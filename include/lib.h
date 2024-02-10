#include "datastructures/abuf.h"
#include "definitions/vimmode.h"
#include "lean_lsp.h"
#include "mathutil.h"
#include "views/ctrlp.h"
#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <json-c/json.h>
#include <json-c/json_util.h>
#include <map>
#include <optional>
#include <stack>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// https://vt100.net/docs/vt100-ug/chapter3.html#CPR
// control key: maps to 1...26,
// 00011111
// #define CTRL_KEY(k) ((k) & ((1 << 6) - 1))
#define CTRL_KEY(k) ((k)&0x1f)

static int utf8_next_code_point_len(const char* str);

static const char* VERSION = "0.0.1";

struct json_object_ptr {
    json_object_ptr(json_object* obj = nullptr)
        : m_obj { obj }
    {
    }

    json_object_ptr& operator=(json_object_ptr const& other)
    {
        if (m_obj) {
            json_object_put(m_obj);
            m_obj = nullptr;
        }
        m_obj = other.m_obj;
        json_object_get(m_obj);
        return *this;
    }
    json_object_ptr(json_object_ptr const& other)
        : m_obj { nullptr }
    {
        *this = other;
    }

    json_object_ptr& operator=(json_object_ptr&& other)
    {
        if (m_obj) {
            json_object_put(m_obj);
            m_obj = nullptr;
        }
        m_obj = other.m_obj;
        other.m_obj = nullptr;
        return *this;
    }
    json_object_ptr(json_object_ptr&& other)
        : m_obj { nullptr }
    {
        *this = std::move(other);
    }

    ~json_object_ptr()
    {
        json_object_put(m_obj);
        m_obj = nullptr;
    }

    operator bool()
    {
        return m_obj != NULL;
    }

    bool operator==(const json_object_ptr& other) const
    {
        return this->m_obj == other.m_obj;
    }

    bool operator!=(const json_object_ptr& other) const
    {
        return this->m_obj != other.m_obj;
    }

    bool operator<(const json_object_ptr& other) const
    {
        return this->m_obj < other.m_obj;
    }

    operator json_object*()
    {
        return m_obj;
    }

private:
    json_object* m_obj;
};
struct LeanServerCursorInfo {
    fs::path file_path;
    int row;
    int col;
};

#define PIPE_WRITE_IX 1
#define PIPE_READ_IX 0

// sequence ID of a LSP request. Used to get a matching response
// when asking for responses.
struct LspRequestId {
    int id = -1;
    LspRequestId() = default;
    LspRequestId(int id)
        : id(id) {};

    LspRequestId& operator=(const LspRequestId& other)
    {
        this->id = other.id;
        return *this;
    }

    bool operator<(const LspRequestId& other) const
    {
        return this->id < other.id;
    }
    bool operator==(const LspRequestId& other) const
    {
        return this->id == other.id;
    }
};

enum class LeanServerInitializedKind {
    Uninitialized,
    Initializing,
    Initialized
};

// https://tldp.org/LDP/lpg/node11.html
struct LeanServerState {
    LeanServerInitializedKind initialized = LeanServerInitializedKind::Uninitialized; // whether this lean server has been initalized.
    LspRequestId initialize_request_id;
    // path to the lakefile associated to this lean server, if it uses one.
    std::optional<fs::path> lakefile_dirpath;
    int parent_buffer_to_child_stdin[2]; // pipe.
    int child_stdout_to_parent_buffer[2]; // pipe.
    int child_stderr_to_parent_buffer[2]; // pipe.
    abuf child_stdout_buffer; // buffer to store child stdout data that has not
                              // been slurped yet.
    abuf child_stderr_buffer; // buffer to store child stderr data that has not
                              // been slurped yet.
    FILE* child_stdin_log_file; // file handle of stdout logging
    FILE* child_stdout_log_file; // file handle of stdout logging
    FILE* child_stderr_log_file; // file handle of stderr logging
    pid_t childpid;
    int next_request_id = 0; // ID that will be assigned to the next request.
    // number of responses that have been read.
    // invariant: nresponses_read < next_request_id. Otherwise we will deadlock.
    int nresponses_read = 0;

    // server-requests that were recieved when trying to wait for a
    // server-response to a client-request
    std::vector<json_object_ptr> unhandled_server_requests;

    // vector of LSP requests to responses.
    std::map<LspRequestId, json_object_ptr> request2response;

    // low-level API to write strings directly.
    int _write_str_to_child(const char* buf, int len) const;
    // low-level API: read from stdout and write into buffer
    // 'child_stdout_buffer'.
    int _read_stdout_str_from_child_nonblocking();
    int _read_stderr_str_from_child_blocking();
    // tries to read the next JSON record from the buffer, in a nonblocking fashion.
    // If insufficied data is in the buffer, then return NULL.
    json_object_ptr _read_next_json_record_from_buffer_nonblocking();
    // read the next json record from the buffer, and if the buffer is not full,
    // then read in more data until successful read. Will either hang indefinitely
    // or return a `json_object*`. Will never return NULL.
    // json_object_ptr _read_next_json_record_from_buffer_blocking();

    // high level APIs to write strutured requests and read responses.
    // write a request, and return the request sequence number.
    // this CONSUMES params.
    LspRequestId write_request_to_child_blocking(const char* method, json_object* params);
    // high level APIs to write a notification.
    // this CONSUMES params.
    void write_notification_to_child_blocking(const char* method,
        json_object* params);
    // performs a tick of processing.
    void tick_nonblocking();

    std::optional<json_object_ptr> read_json_response_from_child_nonblocking(LspRequestId request_id);

    // high level APIs
    void get_tactic_mode_goal_state(LeanServerState state,
        LeanServerCursorInfo cinfo);
    void get_term_mode_goal_state(LeanServerState state,
        LeanServerCursorInfo cinfo);
    void get_completion_at_point(LeanServerState state,
        LeanServerCursorInfo cinfo);
    LeanServerState() {};

    void init(std::optional<fs::path> file_path);

private:
};

namespace tilde {
struct TildeView {
    int scrollback_ix = 0;
    bool quitPressed = false;
    std::vector<std::string> log;
    FILE* logfile = nullptr;
};

extern TildeView g_tilde;

bool tildeWhenQuit(TildeView* tilde);
void tildeOpen(TildeView* tilde);
void tildeHandleInput(TildeView* tilde, int c);
void tildeDraw(TildeView* tilde);
// this sucks, global state.
// void tildeWrite(std::string str);
void tildeWrite(const char* fmt, ...);
void tildeWrite(const std::string& str);
void tildeWrite(const abuf& buf);
};

namespace compileView {
struct CompileView {
    int scrollback_ix = 0;
    bool quitPressed = false;
    struct Item {
        fs::path filepath;
        int row;
        int col;
        std::string info;
    };
    // log
    std::vector<std::string> log;
};

bool compileViewWhenQuit(CompileView* view);
void compileViewOpen(CompileView* view);
void compileViewHandleInput(CompileView* view, int c);
void compileViewDraw(CompileView* view);
};

struct Cursor {
    int row = 0; // index of row. Must be within [0, file->nrows].
    Size<Codepoint> col = Size<Codepoint>(0); // #graphemes to move past from the start of the row to get to the current one.

    Cursor() = default;
    Cursor(int row, int col)
        : row(row)
        , col(Size<Codepoint>(col)) {};

    bool operator==(const Cursor& other) const
    {
        return row == other.row && col == other.col;
    }

    bool operator!=(const Cursor& other) const
    {
        return !(*this == other);
    }
};

// Store the current time, and tells when it is legal to run another operation.
struct Debouncer {
    // return if debounce is possible. If this returns `true`, the client is
    // expected to perform the action.
    // Prefer to use this API as follows:
    // 1) if (debouncer.shouldAct()) { /* perform action */ }
    // 2) if (!debouncer.shouldAct()) { return; } /* perform action */
    bool shouldAct()
    {
        timespec tcur;
        get_time(&tcur);
        const long elapsed_nanosec = tcur.tv_nsec - last_acted_time.tv_nsec;
        const long elapsed_sec = tcur.tv_sec - last_acted_time.tv_sec;
        // we have spent as much time as we wanted.
        if (elapsed_sec >= debounce_sec && elapsed_nanosec >= debounce_nanosec) {
            last_acted_time = tcur;
            return true;
        }
        return false;
    }

    static long millisToNanos(long millis)
    {
        return millis * 1000000;
    }

    Debouncer(long sec, long nanosec)
        : debounce_sec(sec)
        , debounce_nanosec(nanosec)
    {
        last_acted_time.tv_sec = last_acted_time.tv_nsec = 0;
    };

    static void get_time(timespec* ts)
    {
        if (clock_gettime(CLOCK_REALTIME, ts) == -1) {
            perror("unable to get time from clock.");
            exit(1);
        }
    }

private:
    const int debounce_sec; // debounce duration, seconds.
    const int debounce_nanosec; // debounce duration, nanoseconds.
    timespec last_acted_time;
};

// T is a memento, from which the prior state can be
// entirely reconstructed.
template <typename T>
struct Undoer : public T {
public:
    // invariant: once we are `inUndoRedo`, the top of the `undoStack`
    // is the current state.
    void doUndo()
    {
        if (undoStack.empty()) {
            return;
        }
        if (!this->inUndoRedo) {
            this->inUndoRedo = true;
            // we are entering into undo/redo. Save the state right before
            // we began undo/redo, so that the user can redo their way
            // back to the 'earliest' state.
            T cur = getCheckpoint();
            redoStack.push(cur);

            T prev = undoStack.top();
            this->setCheckpoint(prev); // apply to setup the invariant.
        } else {
            assert(this->inUndoRedo);
            if (this->undoStack.size() == 1) {
                return; // this is already the state.
            }
            assert(this->undoStack.size() >= 2);
            // once the user has started undoing, then can only stop by
            // creating an undo memento.
            T cur = undoStack.top();
            redoStack.push(cur); // push the checkpoint of our state.
            undoStack.pop(); // pop cur.
            T prev = undoStack.top();
            this->setCheckpoint(prev); // apply.
        }
    }

    void doRedo()
    {
        if (redoStack.empty()) {
            return;
        }
        assert(this->inUndoRedo);
        T val = redoStack.top();
        undoStack.push(val); // push into redos.

        redoStack.pop();
        this->setCheckpoint(val); // apply the invariant.
    }

    // save the current state for later undoing and redoing.
    void mkUndoMemento()
    {
        // abort being in undo/redo mode.
        this->inUndoRedo = false;
        redoStack = {}; // nuke redo stack.

        T cur = getCheckpoint();
        if (!undoStack.empty()) {
            T& top = undoStack.top();
            if (top == cur) {
                return;
            } // state has not changed, no point.
        }
        undoStack.push(cur);
    }

    // save the current state, and debounce the save by 1 second.
    void mkUndoMementoRecent()
    {
        // if we are in undo/redo, then we should not *automatically* cause us to
        // quit undoRedo by making a memento.
        if (this->inUndoRedo) {
            return;
        }
        if (!debouncer.shouldAct()) {
            return;
        }
        mkUndoMemento();
    }

    Undoer() { }
    virtual ~Undoer() { }

protected:
    virtual T getCheckpoint()
    {
        return *(T*)(this);
    };
    virtual void setCheckpoint(T state)
    {
        *(T*)(this) = state;
    };

private:
    bool inUndoRedo = false; // if we are performing undo/redo.
    std::stack<T> undoStack; // stack of undos.
    std::stack<T> redoStack; // stack of redos.
    // make it greater than `0.1` seconds so it is 2x the perceptible limit
    // for humans. So it is a pause, but not necessarily a long one.
    Debouncer debouncer = Debouncer(0, Debouncer::millisToNanos(150));
};

enum InfoViewTab {
    IVT_Tactic, // show the current tactic state in the info view.
    IVT_Hover,
    IVT_Messages, // show messages form the LSP server in the info view.
    IVT_NumTabs
};

struct FileConfig;
static InfoViewTab infoViewTabCycleNext(FileConfig* f, InfoViewTab t);
static InfoViewTab infoViewTabCyclePrevious(FileConfig* f, InfoViewTab t);

struct LspNonblockingResponse {
    LspRequestId request;
    std::optional<json_object_ptr> response;
    LspNonblockingResponse()
        : request(-1)
        , response(std::nullopt) {};
    LspNonblockingResponse(LspRequestId request)
        : request(request)
        , response(std::nullopt) {};
};

// returns true if it was filled in this turn.
static bool whenFillLspNonblockingResponse(LeanServerState& state, LspNonblockingResponse& o)
{
    if (o.response.has_value()) {
        return false;
    }

    auto it = state.request2response.find(o.request);
    if (it != state.request2response.end()) {
        o.response = it->second;
        return true;
    }
    return false;
}

// NOTE:
// TextDocument for LSP does not need to be cached, as its value monotonically increases,
// even during undo/redo.
struct FileConfigUndoState {
    std::vector<abuf> rows;
    Cursor cursor;
    int cursor_render_col = 0;
    int scroll_row_offset = 0;
    int scroll_col_offset = 0;

    // TODO: should we use a different fn rather than `==`?
    bool operator==(const FileConfigUndoState& other) const
    {
        return rows == other.rows; // only file state is part of undo state for debouncing.
    }
};

fs::path ctrlpGetGoodRootDirAbsolute(const fs::path absolute_startdir);

// NOTE: in sublime text, undo/redo is a purely *file local* idea.
// Do I want a *global* undo/redo? Probably not, no?
// TODO: think about just copying the sublime text API :)
struct FileLocation;
struct FileConfig : public Undoer<FileConfigUndoState> {
    FileConfig(FileLocation loc);

    // offset for scrolling.
    int scroll_row_offset = 0;
    // column offset for scrolling. will render from col_offset till endof row.
    // TODO: this should count in codepoints?
    int scroll_col_offset = 0;

    fs::path absolute_filepath;

    // lean server for file.
    LeanServerState lean_server_state;

    // TextDocument for LSP
    int lsp_file_version = -1;

    // diagonstics from LSP.
    std::vector<LspDiagnostic> lspDiagnostics;

    Cursor leanInfoViewRequestedCursor; // the cursor at which the info view was requested.
    LspNonblockingResponse leanInfoViewPlainGoal;
    LspNonblockingResponse leanInfoViewPlainTermGoal;
    LspNonblockingResponse leanHoverViewHover;
    // TODO: implement definition
    InfoViewTab infoViewTab = IVT_Tactic;
    LspNonblockingResponse leanGotoRequest;
    // TODO: implement completion.
    LspNonblockingResponse leanCompletionViewCompletion;

    // file progress from LSP.
    // contains the start and end ranges, and whether the processing is finished.
    struct ProgressBar {
        int startRow = 0;
        int endRow = 0;
        bool finished = false;
    };
    ProgressBar progressbar;

    bool isSaveDirty() const
    {
        return this->_is_dirty_save;
    }

    bool isLeanSyncDirty() const
    {
        return this->_is_dirty_lean_sync;
    }

    void undirtyLeanSync()
    {
        this->_is_dirty_lean_sync = false;
    }
    // if 'b' is true, then mark the state as dirty.
    // if 'b' is false, then leave the dirty state as-is.
    void makeDirty()
    {
        this->_is_dirty_save = true;
        this->_is_dirty_lean_sync = true;
    }

    bool whenDirtySave()
    {
        bool out = _is_dirty_save;
        _is_dirty_save = false;
        return out;
    }

private:
    bool _is_dirty_save = true;
    bool _is_dirty_lean_sync = true;
};

void fileConfigSyncLeanState(FileConfig* file_config);
void fileConfigLaunchLeanServer(FileConfig* file_config);

// represents a file plus a location, used to save history of file opening/closing.
struct FileLocation {
    fs::path absolute_filepath;
    Cursor cursor;

    // completion view needs this default ctor :(
    // TODO: ask sebastian how to avoid this.
    FileLocation() { }

    FileLocation(const FileConfig& file)
        : absolute_filepath(file.absolute_filepath)
        , cursor(file.cursor)
    {
    }

    FileLocation(const FileLocation& other)
    {
        *this = other;
    }

    FileLocation(fs::path absolute_filepath, Cursor cursor)
        : absolute_filepath(absolute_filepath)
        , cursor(cursor) {};

    FileLocation& operator=(const FileLocation& other)
    {
        this->absolute_filepath = other.absolute_filepath;
        this->cursor = other.cursor;
        return *this;
    }

    bool operator==(const FileLocation& other) const
    {
        return cursor == other.cursor && absolute_filepath == other.absolute_filepath;
    }
};

struct CompletionView {
    // TODO: rename to LspNonblockingResponse
    LspNonblockingResponse completionResponse;
    SingleLineTextArea textArea;
    VimMode previous_state;

    struct Item {
        std::string detail;
        std::string doc;
        int kind;
        std::string label;

        Item(std::string detail, std::string doc, int kind, std::string label)
            : detail(detail)
            , doc(doc)
            , kind(kind)
            , label(label)
        {
        }
    };
    std::vector<Item> items;
    int itemIx = -1;
    bool quitPressed = false;
    bool selectPressed = false;
};

bool completionWhenQuit(CompletionView* view);
bool completionWhenSelected(CompletionView* view);
void completionOpen(CompletionView* view, VimMode previous_state, FileConfig* f);
void completionHandleInput(CompletionView* view, int c);
void completionTickPostKeypress(FileConfig* f, CompletionView* view);
void completionDraw(CompletionView* view);

// unabbrevs[i] ASCII string maps to abbrevs[i] UTF-8 string.
struct AbbreviationDict {
    char** unabbrevs = NULL;
    char** abbrevs = NULL;
    int* unabbrevs_len = NULL; // string lengths of the unabbrevs;
    int nrecords = 0;
    bool is_initialized = false;
};

template <typename T>
struct Zipper {
public:
    int size() const
    {
        return _ts.size();
    }

    T* getFocus()
    {
        if (_ts.size() == 0) {
            return nullptr;
        } else {
            assert(this->_curIx >= 0);
            assert(this->_curIx < this->_ts.size());
            return &_ts[this->_curIx];
        }
    }

    void push_back(const T& t)
    {
        _push_back_no_duplicates(t);
        this->_curIx = _ts.size() - 1;
    }

    void left()
    {
        if (this->_ts.size() == 0) {
            return;
        }
        this->_curIx = clamp0<int>(this->_curIx - 1);
    }

    void right()
    {
        if (this->_ts.size() == 0) {
            return;
        }
        this->_curIx = clampu<int>(this->_curIx + 1, _ts.size() - 1);
    }

    int getIx() const
    {
        return _curIx;
    }

private:
    void _push_back_no_duplicates(const T& t)
    {
        if (_ts.size() > 0 && t == _ts[_ts.size() - 1]) {
            return;
        }
        _ts.push_back(t);
    }
    std::vector<T> _ts;
    int _curIx = -1;
};

struct EditorConfig {
    Zipper<FileLocation> file_location_history;
    VimMode vim_mode = VM_NORMAL;
    struct termios orig_termios;
    int screenrows = 0;
    int screencols = 0;

    char statusmsg[80];
    time_t statusmsg_time = 0;

    fs::path original_cwd; // cwd that the process starts in.
    CtrlPView ctrlp;
    CompletionView completion;
    compileView::CompileView compileView;
    AbbreviationDict abbrevDict;

    EditorConfig() { statusmsg[0] = '\0'; }

    FileConfig* curFile()
    {
        if (files.size() == 0) {
            return NULL;
        } else {
            assert(fileIx < files.size());
            return &files[fileIx];
        }
    };

    void getOrOpenNewFile(FileLocation file_loc, bool isUndoRedo = false)
    {
        tilde::tildeWrite("%s %s:%d:%d", __PRETTY_FUNCTION__, file_loc.absolute_filepath.c_str(),
            file_loc.cursor.row,
            file_loc.cursor.col.size);
        assert(file_loc.absolute_filepath.is_absolute());

        if (!isUndoRedo) {
            if (this->fileIx != -1) {
                assert(this->fileIx >= 0);
                assert(this->fileIx < this->files.size());
                file_location_history.push_back(FileLocation(this->files[this->fileIx]));
            }
            file_location_history.push_back(file_loc);
        }

        // look if file exists.
        // for(int i = 0; i < this->files.size(); ++i) {
        //   if (this->files[i].absolute_filepath == file_loc.absolute_filepath) {
        //     assert(false && "found an existing file");
        //     this->fileIx = i;
        //     this->files[this->fileIx].cursor = file_loc.cursor;
        //     return;
        //   }
        // }
        // we were unable to find an already open file, so make a new file.
        this->files.push_back(FileConfig(file_loc));
        this->fileIx = this->files.size() - 1;
        this->files[this->fileIx].cursor = file_loc.cursor;
    }

    void undoFileMove()
    {
        const int prevIx = file_location_history.getIx();
        file_location_history.left();
        if (prevIx != file_location_history.getIx()) {
            // state changed, so we must have a file of interest.
            this->getOrOpenNewFile(*file_location_history.getFocus(), /*isUndoRedo=*/true);
        }
    }

    void redoFileMove()
    {
        const int prevIx = file_location_history.getIx();
        file_location_history.right();
        if (prevIx != file_location_history.getIx()) {
            // state changed, so we must have a file of interest.
            this->getOrOpenNewFile(*file_location_history.getFocus(), /*isUndoRedo=*/true);
        }
    }

private:
    std::vector<FileConfig> files;
    int fileIx = -1;
};

extern EditorConfig g_editor; // global editor handle.

void enableRawMode();
void disableRawMode();

void write_to_child(const char* buf, int len);
int read_stdout_from_child(const char* buf, int bufsize);
int read_stderr_from_child(const char* buf, int bufsize);
void editorSetStatusMessage(const char* fmt, ...);
char* editorPrompt(const char* prompt);

/*** terminal ***/
void die(const char* fmt, ...);
enum KeyEvent {
    KEYEVENT_PAGE_UP = 1000, // start fom sth that is disjoint from ASCII.
    KEYEVENT_PAGE_DOWN,
    KEYEVENT_ARROW_LEFT,
    KEYEVENT_ARROW_RIGHT,
    KEYEVENT_ARROW_UP,
    KEYEVENT_ARROW_DOWN,
    KEYEVENT_BACKSPACE

};
int editorReadKey();
void getCursorPosition(int* rows, int* cols);
int getWindowSize(int* rows, int* cols);
void fileConfigInsertRowBefore(FileConfig* f, int at, const char* s, size_t len);
void editorDelRow(int at);
// Delete character at location `at`.
// Invariant: `at in [0, row->size)`.
bool is_space_or_tab(char c);
void fileConfigInsertEnterKey(FileConfig* f);
void fileConfigInsertCharBeforeCursor(FileConfig* f, int c); // 32 bit.
void fileConfigDelChar(FileConfig* f);
std::string fileConfigRowsToCppString(FileConfig* file);
void fileConfigRowsToBuf(FileConfig* f, abuf* buf);
void fileConfigDebugPrint(FileConfig* f, abuf* buf);
void fileConfigCursorMoveWordNext(FileConfig* f);
void fileConfigCursorMoveWordPrevious(FileConfig* f);
void fileConfigSave(FileConfig* f);
void fileConfigGotoDefinitionNonblocking(FileConfig* f);
LspPosition cursorToLspPosition(Cursor c);

void editorDraw();
void editorTickPostKeypress();
void editorScroll();
void editorDrawRows(abuf& ab);
void editorDrawStatusBar(abuf& ab);
void editorDrawMessageBar(abuf& ab);
void editorMoveCursor(int key);
void editorProcessKeypress();
char* editorPrompt(const char* prompt);
void initEditor();

// Load the abbreviation dictionary from the filesystem.
// NOTE: this steals the pointer to `o`.
void load_abbreviation_dict_from_json(AbbreviationDict* dict, json_object* o);

// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_file(AbbreviationDict* dict, fs::path abbrev_path);

// returns true if str ends in \<prefix of potential_abbrev>
enum AbbrevMatchKind {
    AMK_NOMATCH = 0,
    AMK_PREFIX_MATCH = 1,
    AMK_EXACT_MATCH = 2, // exact match
    AMK_EMPTY_STRING_MATCH = 3, // match against the empty string.
};

// get length <len> such that
//    buf[:finalix) = "boo \alpha"
//    buf[:finalix - <len>] = "boo" and buf[finalix - len:finalix) = "\alpha".
// this is such that buf[finalix - <len>] = '\' if '\' exists,
//     and otherwise returns <len> = 0.
int suffix_get_unabbrev_len(const char* buf, int finalix, const char* unabbrev, int unabbrevlen);
// return whether there is a suffix of `buf` that looks like `\<unabbrev_prefix>`.
AbbrevMatchKind suffix_is_unabbrev(const char* buf, int finalix, const char* unabbrev, int unabbrevlen);
const char* abbrev_match_kind_to_str(AbbrevMatchKind);

// return the index of the all matches, for whatever match exists. Sorted to be matches
// where the match string has the smallest length to the largest length.
// This ensures that the AMK_EXACT_MATCHes will occur at the head of the list.
void abbrev_dict_get_matching_unabbrev_ixs(AbbreviationDict* dict,
    const char* buf, int finalix, std::vector<int>* matchixs);

struct SuffixUnabbrevInfo {
    AbbrevMatchKind kind = AbbrevMatchKind::AMK_NOMATCH;
    int matchlen = -1; // length of the math if kind is not NOMATCH;
    int matchix = -1; // index of the match if kind is not NOMATCH

    // return the information of having no match.
    static SuffixUnabbrevInfo nomatch()
    {
        return SuffixUnabbrevInfo();
    }
};

// return the best unabbreviation for the suffix of `buf`.
SuffixUnabbrevInfo abbrev_dict_get_unabbrev(AbbreviationDict* dict, const char* buf, int finalix);

// get the path to the executable, so we can build the path to resources.
fs::path get_executable_path();
// get the path to `/path/to/exe/abbreviations.json`.
fs::path get_abbreviations_dict_path();
