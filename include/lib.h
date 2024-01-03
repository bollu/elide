#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <json-c/json.h>
#include <json-c/json_util.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <stack>
#include <unordered_map>
#include "lean_lsp.h"
#include "mathutil.h"
#include <filesystem>
#include <optional>
namespace fs = std::filesystem;

// https://vt100.net/docs/vt100-ug/chapter3.html#CPR
// control key: maps to 1...26,
// 00011111
// #define CTRL_KEY(k) ((k) & ((1 << 6) - 1))
#define CTRL_KEY(k) ((k)&0x1f)

static int utf8_next_code_point_len(const char *str);

static const int NSPACES_PER_TAB = 2;
static const char *VERSION = "0.0.1";


struct json_object_ptr
{
  json_object_ptr(json_object *obj=nullptr): m_obj {obj} {
  }

  json_object_ptr &operator =(json_object_ptr const &other) {
    if(m_obj) {
      json_object_put(m_obj);
      m_obj = nullptr;
    }
    m_obj = other.m_obj;
    json_object_get(m_obj);
    return *this;
  }
  json_object_ptr(json_object_ptr const &other): m_obj {nullptr} {
    *this = other;
  }

  json_object_ptr &operator =(json_object_ptr &&other) {
    if(m_obj) {
      json_object_put(m_obj);
      m_obj = nullptr;
    }
    m_obj = other.m_obj;
    other.m_obj = nullptr;
    return *this;
  }
  json_object_ptr(json_object_ptr &&other): m_obj {nullptr} {
    *this = std::move(other);
  }

  ~json_object_ptr() {
    json_object_put(m_obj);
    m_obj = nullptr;
  }

  operator json_object * () {
    return m_obj;
  }

private:
  json_object *m_obj;
};

struct abuf {
  abuf() = default;
  ~abuf() { free(_buf); }

  static abuf from_steal_str(char *str) {
    abuf out;
    out._buf = str;
    out._len = strlen(str);
    return out;
  }

  static abuf from_copy_str(const char *str) {
    abuf out;
    out._buf = strdup(str);
    out._len = strlen(str);
    return out;
  }

  static abuf from_steal_buf(char *buf, int len) {
    abuf out;
    out._buf = buf;
    out._len = len;
    return out;
  }

  static abuf from_copy_buf(const char *buf, int len) {
    abuf out;
    out.appendbuf(buf, len);
    return out;
  }

  abuf &operator = (const abuf &other) {
    _len = other._len;
    _buf = (char*)realloc(_buf, sizeof(char) * _len);
    if (_len > 0) {
      memcpy(_buf, other._buf, _len);
    }
    this->_is_dirty = other._is_dirty;
    return *this;
  }
  abuf(const abuf &other) {
    *this = other;
  }

  void appendbuf(const char *s, int slen) {
    assert(slen >= 0 && "negative length!");
    if (slen == 0) { return; }
    this->_buf = (char *)realloc(this->_buf, this->_len + slen);
    assert(this->_buf && "unable to append string");
    memcpy(this->_buf + this->_len, s, slen);
    this->_len += slen;
    this->_is_dirty = true;
  }

  void appendbuf(const abuf *other) {
    appendbuf(other->_buf, other->_len);
    this->_is_dirty = true;
  }


  // append a UTF-8 codepoint.
  void appendCodepoint(const char *codepoint) {
    const int len = utf8_next_code_point_len(codepoint);
    this->appendbuf(codepoint, len);
    this->_is_dirty = true;
  }

  void prependbuf(const char *s, int slen) {
    assert(slen >= 0 && "negative length!");
    if (slen == 0) { return; }
    this->_buf = (char *)realloc(this->_buf, this->_len + slen);
    
  // shift data forward.
    for(int i = 0; i < _len; ++i) {
      this->_buf[this->_len - 1 - i] = this->_buf[this->_len + slen - 1 - i];
    }

    for(int i = 0; i < slen; ++i) {
      this->_buf[i] = s[i];
    }

    this->_len += slen;
    this->_is_dirty = true;
  }

  void prependbuf(const abuf *other) {
    prependbuf(other->_buf, other->len());
    this->_is_dirty = true;
  }
  void prependCodepoint(const char *codepoint) {
    const int len = utf8_next_code_point_len(codepoint);
    this->prependbuf(codepoint, len);
    this->_is_dirty = true;
  }

  // TODO: rename API
  Size<Byte> getCodepointBytesAt(Ix<Codepoint> i) const {
    return Size<Byte>(utf8_next_code_point_len(getCodepoint(i)));
  }

  char getByteAt(Ix<Byte> i) const {
    assert(Ix<Byte>(0) <= i);
    assert(i < this->len());
    return this->_buf[i.ix];
  }


  // insert a single codepoint.
  void insertCodepointBefore(Size<Codepoint> at, const char *codepoint) {
    assert(at.size >= 0);
    assert(at.size <= this->_len);

    // TODO: refactor by changing type to `abuf`.
    Size<Byte> n_bufUptoAt = Size<Byte>(0);
    for(Ix<Codepoint> i(0); i < at; i++)  {
      n_bufUptoAt += this->getCodepointBytesAt(i);
    }

    const Size<Byte> nNew_buf(utf8_next_code_point_len(codepoint));
    this->_buf = (char*)realloc(this->_buf, this->_len + nNew_buf.size);
      
    for(int oldix = this->_len - 1; oldix >= n_bufUptoAt.size; oldix--) {
      // push _buf from `i` into `i + nNew_buf`.
      this->_buf[oldix + nNew_buf.size] = this->_buf[oldix];
    }    

    // copy new _buf into into location.
    for(int i = 0; i < nNew_buf.size; ++i)  {
      this->_buf[n_bufUptoAt.size + i] = codepoint[i];
    }
    this->_len += nNew_buf.size;
    this->_is_dirty = true;

  }

  void delCodepointAt(Ix<Codepoint> at) {
    // TODO: refactor by changing type to `abuf`.
    Size<Byte> startIx = Size<Byte>(0);
    for(Ix<Codepoint> i(0); i < at; i++)  {
      startIx += this->getCodepointBytesAt(i);
    }

    const Size<Byte> ntoskip = this->getCodepointBytesAt(at);
    
    for(int i = startIx.size; i < this->_len - ntoskip.size; i++) {
      this->_buf[i] = this->_buf[i + ntoskip.size];
    }    
    this->_len -= ntoskip.size;
    // resize to eliminate leftover.
    this->_buf = (char *)realloc(this->_buf, this->_len);
    this->_is_dirty = true;
  }


  // append a sequence of n UTF-8 codepoints.
  void appendCodepoints(const char *codepoint, int n) {
    int delta = 0;
    for(int i = 0; i < n; ++i)  {
      delta += utf8_next_code_point_len(codepoint);
      this->appendCodepoint(codepoint + delta);
    }
    this->_is_dirty = true;
  }

  void appendChar(char c) {
    this->appendbuf(&c, 1);
    this->_is_dirty = true;
  }

  // take the substring of [start,start+len) and convert it to a string.
  // pointer returned must be free.
  char *to_string_start_len(int start, int slen) const {
    slen = clamp0u<int>(slen, this->len() - start); 
      // std::max<int>(0, std::min<int>(slen, this->_len - start));
    assert(slen >= 0);
    char *out = (char *)calloc(slen + 1, sizeof(char));
    if (this->_buf != NULL) {
      memcpy(out, this->_buf + start, slen);
    }
    return out;
  }

  // take substring [start, buflen).
  char *to_string_from_start_ix(int startix) const {
    return to_string_start_len(startix, this->_len);
  }

  // take substring [0, slen)
  char *to_string_len(int slen) const {
    return to_string_start_len(0, slen);
  }

  // TODO: rename to 'to_c_str()'
  // convert buffer to string.
  char *to_string() const {
    return to_string_start_len(0, this->_len);
  }

  std::string to_std_string() const {
    return std::string(this->_buf, this->_buf + this->_len);
  }

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int find_sub_buf(const char *findbuf, int findbuf_len, int begin_ix) const {
    for (int i = begin_ix; i < this->_len; ++i) {
      int match_len = 0;
      while (i + match_len < this->_len && match_len < findbuf_len) {
        if (this->_buf[i + match_len] == findbuf[match_len]) {
          match_len++;
        } else {
          break;
        }
      }
      // we matched, return index.
      if (match_len == findbuf_len) { return i; }
    };
    // no match, return -1;
    return -1;
  };

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int find_substr(const char *findstr, int begin_ix) const {
    return find_sub_buf(findstr, strlen(findstr), begin_ix);
  }

  // Return first index `i >= begin_ix` such that `buf[i] = c`.
  // Return `-1` otherwise.
  int find_char(char c, int begin_ix) const { return find_sub_buf(&c, 1, begin_ix); }


  // append a string onto this string.
  void appendstr(const char *s) { 
    appendbuf(s, strlen(s));
    this->_is_dirty = true;
  }

  // append a format string onto this string. Truncate to length 'len'.
  void appendfmtstr(int len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt); 
    char *buf = (char*)malloc(sizeof(char) * len);
    vsnprintf(buf, len, fmt, args);
    va_end(args);
    appendstr(buf);
    free(buf); 
    this->_is_dirty = true;
  }


  // drop a prefix of the buffer. If `drop_len = 0`, then this operation is a
  // no-op.
  void dropNBytesMut(int drop_len) {
    char *bnew = (char *)malloc(sizeof(char) * (_len - drop_len));
    memcpy(bnew, this->_buf + drop_len, this->_len - drop_len);
    free(this->_buf);
    this->_buf = bnew;
    this->_len -= drop_len;
    this->_is_dirty = true;
  }

  int len() const {
    return this->_len;
  }

  const char *buf() const {
    return this->_buf;
  }

  // if dirty, return `true` and reset the dirty state of `abuf`.
  bool whenDirty() {
    const bool out = this->_is_dirty;
    this->_is_dirty = false;
    return out;
  }


  const char *getCodepointFromRight(Ix<Codepoint> ix) const {
    assert(ix < this->ncodepoints());
    this->getCodepoint(this->ncodepoints().mirrorIx(ix));
  }

  bool operator == (const abuf &other) const {
    if (this->_len != other._len) { return false; }
    for(int i = 0; i < this->_len; ++i) {
      if (this->_buf[i] != other._buf[i]) { return false; }
    } 
    return true;
  }
  
  Size<Byte> nbytes() const {
    return Size<Byte>(this->_len);
  }


  Size<Codepoint> ncodepoints() const {
    int count = 0;
    int ix = 0;
    while(ix < this->_len) {
      ix += utf8_next_code_point_len(this->_buf + ix);
      count++;
    }
    assert(ix == this->_len);
    return Size<Codepoint>(count);
  }

  const char *debugToString() const {
    char *str = (char*)malloc(this->nbytes().size + 1);
    for(int i = 0; i < this->nbytes().size; ++i) {
      str[i] = this->_buf[i];
    }
    str[this->nbytes().size] = '\0';
    return str;
  }

  const char *getCodepoint(Ix<Codepoint> ix) const {
    assert(ix < this->ncodepoints());
    int delta = 0;
    for(Ix<Codepoint> i(0); i < ix; i++) {
      delta += utf8_next_code_point_len(this->_buf + delta);
    }
    return this->_buf + delta;
  }

  // TODO: think about why we need the other version.
  // 'Clearly', this version is correct, since even when 'ix = len',
  // we will return a valid pointer (end of list).
  const char *getCodepoint(Size<Codepoint> sz) const {
    assert(sz <= this->ncodepoints());
    int delta = 0;
    for(Ix<Codepoint> i(0); i < sz; i++) {
      delta += utf8_next_code_point_len(this->_buf + delta);
    }
    return this->_buf + delta;
  }


  // get the raw _buf. While functionally equivalent to
  // getCodepoint, this gives one more license to do things like `memcpy`
  // and not have it look funny.
  const char *getRawBytesPtrUnsafe() const {
    return this->_buf;
  }

  // // TODO: rename API
  // Size<Byte> getCodepointBytesAt(Ix<Codepoint> i) const {
  //   return Size<Byte>(utf8_next_code_point_len(getCodepoint(i)));
  // }
  Size<Byte> getBytesTill(Size<Codepoint> n) const {
    Size<Byte> out(0);
    for(Ix<Codepoint> i(0); i < n; ++i)  {
      out += getCodepointBytesAt(i);
    }
    return out;
  }


  int rxToCx(int rx) const {
    int cur_rx = 0;
    for (int cx = 0; cx < this->_len; cx++) {
      if (_buf[cx] == '\t') {
        cur_rx += (NSPACES_PER_TAB - 1) - (cur_rx % NSPACES_PER_TAB);
      }
      cur_rx++;
      if (cur_rx > rx) {
        return cx;
      }
    }
    assert(false && "rx value that is out of range!");
  }

  int cxToRx(Size<Codepoint> cx) const {
    assert(cx <= this->ncodepoints());
    int rx = 0;
    char *p = this->_buf;
    for (Ix<Codepoint> j(0); j < cx; ++j) {
      if (*p == '\t') {
        rx += NSPACES_PER_TAB - (rx % NSPACES_PER_TAB);
      } else {
        rx += 1; // just 1.
      }
      p += this->getCodepointBytesAt(j).size;
    }
    return rx;
  }

  // it is size, since we can ask to place the data at the *end* of the string, past the
  // final.
  void insertByte(Size<Codepoint> at, int c) {
    assert(at.size >= 0);
    assert(at.size <= this->_len);
    _buf = (char *)realloc(_buf, this->_len + 1);

    Size<Byte> byte_at(0);
    for(Ix<Codepoint> i(0); i < at; ++i) {
      byte_at += this->getCodepointBytesAt(i);
    }

    for(int i = this->_len; i >= byte_at.size+1; i--) {
      this->_buf[i] = this->_buf[i - 1];
    }    
    _buf[byte_at.size] = c;
    this->_len += 1;
    this->_is_dirty = true;
  }


  // set the data.
  // TODO: think if we should expose _buf API.
  // TODO: force copy codepoint by codepoint.
  void setBytes(const char *buf, int len) {
    this->_len = len;
    _buf = (char *)realloc(_buf, sizeof(char) * this->_len);
    for(int i = 0; i < len; ++i) {
      _buf[i] = buf[i];
    }
    this->_is_dirty = true;

  }

  abuf takeNBytes(Size<Byte> bytes) const {
    assert(bytes.size >= 0);
    assert(bytes.size <= this->_len);
    abuf buf;
    buf._len = bytes.size;
    buf._buf = (char*)calloc(sizeof(char), buf._len);
    buf._is_dirty  = true;
    for(int i = 0; i < buf._len; ++i) {
      buf._buf[i] = this->_buf[i];
    }
    return buf;
  };

  // truncate to `ncodepoints_new` codepoints.
  void truncateNCodepoints(Size<Codepoint> ncodepoints_new) {
    assert(ncodepoints_new <= this->ncodepoints());
    Size<Byte> nbytes(0);
    for(Ix<Codepoint> i(0); i < ncodepoints_new; i++)  {
      nbytes += this->getCodepointBytesAt(i);
    }
    this->_buf = (char*)realloc(this->_buf, nbytes.size);
    this->_len = nbytes.size;
    this->_is_dirty = true;
  }


protected:
  char *_buf = nullptr;
  int _len = 0;
  bool _is_dirty = true;

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
  int id = -1;;
  LspRequestId()  = default;
  LspRequestId(int id) : id(id) {};
};


// https://tldp.org/LDP/lpg/node11.html
struct LeanServerState {
  bool initialized = false; // whether this lean server has been initalized.
  // path to the lakefile associated to this lean server, if it uses one.
  std::optional<fs::path> lakefile_dirpath;
  int parent_buffer_to_child_stdin[2];  // pipe.
  int child_stdout_to_parent_buffer[2]; // pipe.
  int child_stderr_to_parent_buffer[2]; // pipe.
  abuf child_stdout_buffer;    // buffer to store child stdout data that has not
                               // been slurped yet.
  abuf child_stderr_buffer;    // buffer to store child stderr data that has not
                               // been slurped yet.
  FILE *child_stdin_log_file;  // file handle of stdout logging
  FILE *child_stdout_log_file; // file handle of stdout logging
  FILE *child_stderr_log_file; // file handle of stderr logging
  pid_t childpid;
  int next_request_id = 0; // ID that will be assigned to the next request.
  // number of responses that have been read.
  // invariant: nresponses_read < next_request_id. Otherwise we will deadlock.
  int nresponses_read = 0;

  // server-requests that were recieved when trying to wait for a 
  // server-response to a client-request
  std::vector<json_object_ptr> unhandled_server_requests;

  // low-level API to write strings directly.
  int _write_str_to_child(const char *buf, int len) const;
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
  json_object_ptr _read_next_json_record_from_buffer_blocking();

  // high level APIs to write strutured requests and read responses.
  // write a request, and return the request sequence number.
  // this CONSUMES params.
  LspRequestId write_request_to_child_blocking(const char *method, json_object *params);
  // high level APIs to write a notification.
  // this CONSUMES params.
  void write_notification_to_child_blocking(const char *method,
                                            json_object *params);
  json_object_ptr read_json_response_from_child_blocking(LspRequestId request_id);

  // high level APIs
  void get_tactic_mode_goal_state(LeanServerState state,
                                  LeanServerCursorInfo cinfo);
  void get_term_mode_goal_state(LeanServerState state,
                                LeanServerCursorInfo cinfo);
  void get_completion_at_point(LeanServerState state,
                               LeanServerCursorInfo cinfo);

  static LeanServerState init(std::optional<fs::path> file_path);
private:
};

enum VimMode {
  VM_NORMAL, // mode where code is only viewed and locked for editing.
  VM_INSERT, // mode where code is edited.
  VM_INFOVIEW_DISPLAY_GOAL, // mode where infoview is shown.
  VM_COMPLETION, // mode where code completion results show up.
  VM_CTRLP, // mode where control-p search anything results show up.
  VM_TILDE, // mode where the editor logs its info, like the infamous quake `~`.
};

namespace tilde {
struct TildeView {
  // if there is a scrollback_ix, then we are scrolling back.
  // if there is no scrollback_ix, then we are following output.
  std::optional<int> scrollback_ix = {};
  bool quitPressed = false;
  std::vector<std::string> log;
  FILE *logfile = nullptr;
};

extern TildeView g_tilde;

bool tildeWhenQuit(TildeView *tilde);
void tildeOpen(TildeView *tilde);
void tildeHandleInput(TildeView *tilde, int c);
void tildeDraw(TildeView *tilde);
// this sucks, global state.
// void tildeWrite(std::string str);
void tildeWrite(const char *fmt, ...);
void tildeWrite(const std::string &str);
void tildeWrite(const abuf &buf);
};

struct Cursor {
  Size<Codepoint> col = Size<Codepoint>(0); // number of graphemes to move past from the start of the row to get to the current one.
  int row = 0; // index of row. Must be within [0, file->nrows].

  bool operator == (const Cursor &other) {
    return row == other.row && col == other.col;
  }
};


// Store the current time, and tells when it is legal to run another operation.
struct Debouncer {
  // return if debounce is possible. If this returns `true`, the client is
  // expected to perform the action.
  // Prefer to use this API as follows:
  // 1) if (debouncer.shouldAct()) { /* perform action */ }
  // 2) if (!debouncer.shouldAct()) { return; } /* perform action */
  bool shouldAct() {
    timespec tcur;
    get_time(&tcur);
    const long elapsed_nanosec = tcur.tv_nsec - last_acted_time.tv_nsec;
    const long elapsed_sec = tcur.tv_sec - last_acted_time.tv_sec;
    // we have spent as much time as we wanted.
    if(elapsed_sec >= debounce_sec && elapsed_nanosec >= debounce_nanosec) {
      last_acted_time = tcur;
      return true;
    }
    return false;
  }

  static long millisToNanos(long millis) {
    return millis * 1000000;
  }

  Debouncer(long sec, long nanosec) : 
    debounce_sec(sec), debounce_nanosec(nanosec) {
       last_acted_time.tv_sec = last_acted_time.tv_nsec = 0;
   };

  static void get_time(timespec *ts) {
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
template<typename T>
struct Undoer : public T {
public:
  // invariant: once we are `inUndoRedo`, the top of the `undoStack`
  // is the current state.
  void doUndo() {
    if (undoStack.empty()) { return; }
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

  void doRedo() {
    if (redoStack.empty()) { return; }
    assert(this->inUndoRedo);
    T val = redoStack.top();
    undoStack.push(val); // push into redos.

    redoStack.pop();
    this->setCheckpoint(val); // apply the invariant.
  }

  // save the current state for later undoing and redoing.
  void mkUndoMemento() {
    // abort being in undo/redo mode.
    this->inUndoRedo = false;
    redoStack = {};  // nuke redo stack.

    T cur = getCheckpoint();
    if (!undoStack.empty()) {
      T &top = undoStack.top();
      if (top == cur) { return; } // state has not changed, no point.  
    }
    undoStack.push(cur);
  }

  // save the current state, and debounce the save by 1 second.
  void mkUndoMementoRecent() {
    // if we are in undo/redo, then we should not *automatically* cause us to
    // quit undoRedo by making a memento.
    if (this->inUndoRedo) { return; }
    if (!debouncer.shouldAct()) { return; }
    mkUndoMemento();
  }

  Undoer() {}


protected:
  virtual T getCheckpoint() {
    return *(T*)(this);
  };
  virtual void setCheckpoint(T state) {
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
static InfoViewTab infoViewTabCycleNext(FileConfig *f, InfoViewTab t);
static InfoViewTab infoViewTabCyclePrevious(FileConfig *f, InfoViewTab t);

// a struct to encapsulate a child proces that is line buffered,
// which is a closure plus a childpid. 
struct RgProcess {
  // whether process has been initialized.
  bool running = false;
  // stdout buffer of the child that is stored here before being processed.
  abuf child_stdout_buffer;
  int child_stdout_to_parent_buffer[2]; // pipe.

  // PID of the child process that is running.
  pid_t childpid;
  // lines streamed from `rg` stdout.
  std::vector<abuf> lines;
  int selectedLine; // currently selected line. Bounds are `[0, lines.size())`

  // start the process, and run it asynchronously.
  void execpAsync(const char *working_dir, std::vector<std::string> args);
  // kills the process synchronously.
  void killSync();

  // attempt to read a single line of input from `rg`.
  // return if line was succesfully read.
  bool readLineNonBlocking();

  // attempt to lines of input from `rg`, and prints the number of lines
  // that were successfully added to `this->lines`.
  int readLinesNonBlocking();
  
  // TODO: implement this so we can be a little bit smarter,
  // and show an indication that we have finished.  
  // // returns whether the process is running.
  // bool isRunningNonBlocking();
private:
  // read from child in a nonblocking fashion.
  int _read_stdout_str_from_child_nonblocking();
};


// NOTE: 
// TextDocument for LSP does not need to be cached, as its value monotonically increases,
// even during undo/redo.
struct FileConfigUndoState {
  std::vector<abuf> rows; 
  Cursor cursor; 
  int cursor_render_col = 0;
  int scroll_row_offset = 0;
  int scroll_col_offset = 0;

  json_object_ptr leanInfoViewPlainGoal;
  json_object_ptr leanInfoViewPlainTermGoal;
  json_object_ptr leanHoverViewHover;
  // TODO: implement definition
  InfoViewTab infoViewTab = IVT_Tactic;
  json_object_ptr leanGotoViewDefinition;
  json_object_ptr leanGotoViewDeclaration;
  // TODO: implement completion.
  json_object_ptr leanCompletionViewCompletion;

  // TODO: should we use a different fn rather than `==`?
  bool operator == (const FileConfigUndoState &other) const {
    return rows == other.rows; // only file state is part of undo state for debouncing.

  }
};

enum TextAreaMode {
  TAM_Normal,
  TAM_Insert
};

static const char *textAreaModeToString(TextAreaMode mode) {
  switch(mode) {
  case TAM_Normal: return "normal";
  case TAM_Insert: return "insert";
  }
  assert(false && "unreachable: must handle all 'TextAreaMode's.");
}

// data associated to the per-file `CtrlP` view.
struct CtrlPView {
  VimMode previous_state;
  TextAreaMode textAreaMode;
  fs::path absolute_cwd; // default working directory for the search.
  abuf textArea;
  Size<Codepoint> textCol;
  RgProcess rgProcess;
  bool quitPressed = false;
  bool selectPressed = false;


  struct RgArgs {
    std::vector<std::string> pathGlobPatterns; // patterns to filter the search in, added with '-g'.
    std::vector<std::string> directoryPaths; // directories to search in.
    std::string fileContentPattern;

    void debugPrint(abuf *buf) const {
      buf->appendfmtstr(100, "pathGlobPatterns [n=%d]\n", (int)pathGlobPatterns.size());
      for(const std::string &s : pathGlobPatterns) {
        buf->appendfmtstr(100, "  . %s\n", s.c_str());
      }
      buf->appendfmtstr(100, "directoryPaths [n=%d]\n", (int)directoryPaths.size());
      for(const std::string &s : directoryPaths) {
        buf->appendfmtstr(100, "  . %s\n", s.c_str());
      }
      buf->appendfmtstr(100, "fileContentPattern '%s'", fileContentPattern.c_str());
    } 

    abuf debugPrint() const {
      abuf buf;
      this->debugPrint(&buf);
      return buf;
    }
  };
  // #foo
  // *.lean#bar|baz@../@../../ (TODO: pressing TAB should cycle between the components).
  // (GLOBPAT",")*("@"FILEPAT|"#"TEXTPAT|","GLOBPAT)*
  // FILENAMEPAT := <str>
  // FSPAT := "@"<str>
  // TEXTPAT := "#"<str>
  static RgArgs parseUserCommand(abuf buf);

  // rg TEXT_STRING PROJECT_PATH -g FILE_PATH_GLOB # find all files w/contents matching pattern.
  // rg PROJECT_PATH --files -g FILE_PATH_GLOB # find all files w/ filename matching pattern.
  static std::vector<std::string> rgArgsToCommandLineArgs(RgArgs invoke);
};

// convert level 'quitPressed' into edge trigger.
bool ctrlpWhenQuit(CtrlPView *view);
bool ctrlpWhenSelected(CtrlPView *view);
struct FileLocation;
FileLocation ctrlpGetSelectedFileLocation(const CtrlPView *view);
void ctrlpOpen(CtrlPView *view, VimMode previous_state, fs::path cwd);
void ctrlpHandleInput(CtrlPView *view, int c);
void ctrlpDraw(CtrlPView *view);

// NOTE: in sublime text, undo/redo is a purely *file local* idea.
// Do I want a *global* undo/redo? Probably not, no?
// TODO: think about just copying the sublime text API :)
struct FileLocation;
struct FileConfig : public Undoer<FileConfigUndoState> {
  FileConfig(FileLocation loc);

  bool is_initialized = false;

  // offset for scrolling.
  int scroll_row_offset = 0;
  // column offset for scrolling. will render from col_offset till endof row.
  // TODO: this should count in codepoints?
  int scroll_col_offset = 0;
  
  fs::path absolute_filepath;

  // lean server for file.
  LeanServerState lean_server_state;

  // TextDocument for LSP
  TextDocumentItem text_document_item;

  // if 'b' is true, then mark the state as dirty.
  // if 'b' is false, then leave the dirty state as-is.
  void makeDirty() {
    this->_is_dirty_save = true;
    this->_is_dirty_info_view = true;
  }

  bool whenDirtySave() { 
    bool out = _is_dirty_save; _is_dirty_save = false; return out;
  }
  
  bool whenDirtyInfoView() { 
    bool out = _is_dirty_info_view; _is_dirty_info_view = false; return out;
  }
private:
  bool _is_dirty_save = false;
  bool _is_dirty_info_view = false;
};

// represents a file plus a location, used to save history of file opening/closing.
struct FileLocation {
  fs::path absolute_filepath;
  Cursor cursor;

  FileLocation(const FileConfig &file) : 
    absolute_filepath(file.absolute_filepath), cursor(file.cursor) {}

  FileLocation(const FileLocation &other) {
    *this = other;
  }

  FileLocation(fs::path absolute_filepath, Cursor cursor) : 
    absolute_filepath(absolute_filepath), cursor(cursor) {};

  FileLocation &operator =(const FileLocation &other) {
    this->absolute_filepath = other.absolute_filepath;
    this->cursor = other.cursor;
    return *this;
  }

  bool operator == (const FileLocation &other) {
    return cursor == other.cursor && absolute_filepath == other.absolute_filepath;
  }

};



// unabbrevs[i] ASCII string maps to abbrevs[i] UTF-8 string.
struct AbbreviationDict {
  char **unabbrevs = NULL; 
  char **abbrevs = NULL;
  int *unabbrevs_len = NULL; // string lengths of the unabbrevs;
  int nrecords = 0;
  bool is_initialized = false;
};


template<typename T>
struct Zipper {
public:
  int size() const {
    return _ts.size();
  }

  T* getFocus() {
    if (_ts.size() == 0) {
      return nullptr;
    } else {
      assert(this->_curIx >= 0);
      assert(this->_curIx < this->_ts.size());
      return &_ts[this->_curIx];
    }
  }

  void push_back(const T &t) {
    _push_back_no_duplicates(t);
    this->_curIx = _ts.size() - 1;
  }

  void left() {
    if (this->_ts.size() == 0) { return; }
      this->_curIx = clamp0<int>(this->_curIx - 1);
  }

  void right() {
    if (this->_ts.size() == 0) { return; }
    this->_curIx = clampu<int>(this->_curIx + 1, _ts.size() - 1);
  }

  int getIx() const {
    return _curIx;
  }

private:

  void _push_back_no_duplicates(const T &t) {
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

  FileConfig *curFile() {
    if (files.size() == 0) { 
      return NULL;
    } else { 
      assert(fileIx < files.size());
      return &files[fileIx];
    }
  };


  void getOrOpenNewFile(FileLocation file_loc, bool isUndoRedo=false) {
    assert(file_loc.absolute_filepath.is_absolute());

    if (!isUndoRedo) {
      if (this->fileIx != -1) {
        assert(this->fileIx < this->files.size());
        file_location_history.push_back(FileLocation(this->files[fileIx]));
      }
      file_location_history.push_back(file_loc);
    }

    // look if file exists.
    for(int i = 0; i < this->files.size(); ++i) {
      if (this->files[i].absolute_filepath == file_loc.absolute_filepath) {
        fileIx = i;
        // TODO: this separation is kinda jank, fix it.
        this->files[fileIx].cursor = file_loc.cursor;
        return;
      }
    }
    // we were unable to find an already open file, so make a new file.
    fileIx = this->files.size();
    this->files.push_back(FileConfig(file_loc));
    this->files[fileIx].cursor = file_loc.cursor;
  }

  void undoFileMove() {
    const int prevIx = file_location_history.getIx();
    file_location_history.left();
    if (prevIx != file_location_history.getIx()) {
      // state changed, so we must have a file of interest.
      this->getOrOpenNewFile(*file_location_history.getFocus(), /*isUndoRedo=*/true);
    }
  }

  void redoFileMove() {
    const int prevIx = file_location_history.getIx();
    file_location_history.right();
    if (prevIx != file_location_history.getIx()) {
      // state changed, so we must have a file of interest.
      this->getOrOpenNewFile(*file_location_history.getFocus(), /*isUndoRedo=*/true);
    }
  }

  fs::path original_cwd; // cwd that the process starts in.
  CtrlPView ctrlp;
  AbbreviationDict abbrevDict;
  EditorConfig() { statusmsg[0] = '\0'; }
private:
  std::vector<FileConfig> files;
  int fileIx = -1;
};

extern EditorConfig g_editor; // global editor handle.

void enableRawMode();
void disableRawMode();

void write_to_child(const char *buf, int len);
int read_stdout_from_child(const char *buf, int bufsize);
int read_stderr_from_child(const char *buf, int bufsize);
void editorSetStatusMessage(const char *fmt, ...);
char *editorPrompt(const char *prompt);


/*** terminal ***/
void die(const char *fmt, ...);
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
void getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);
void fileConfigInsertRowBefore(FileConfig *f, int at, const char *s, size_t len);
void editorDelRow(int at);
// Delete character at location `at`.
// Invariant: `at in [0, row->size)`.
bool is_space_or_tab(char c);
void fileConfigInsertEnterKey(FileConfig *f);
void fileConfigInsertCharBeforeCursor(FileConfig *f, int c); // 32 bit.
void fileConfigDelChar(FileConfig *f);
void fileConfigRowsToBuf(FileConfig *f, abuf *buf);
void fileConfigDebugPrint(FileConfig *f, abuf *buf); 
void fileConfigCursorMoveWordNext(FileConfig *f);
void fileConfigCursorMoveWordPrevious(FileConfig *f);
void fileConfigSave(FileConfig *f);
std::optional<FileLocation> fileConfigGotoDefinition(FileConfig *f);
Position cursorToPosition(Cursor c);

void editorDraw();
void editorScroll();
void editorDrawRows(abuf &ab);
void editorDrawStatusBar(abuf &ab);
void editorDrawMessageBar(abuf &ab);
void editorMoveCursor(int key);
void editorProcessKeypress();
char *editorPrompt(const char *prompt);
void initEditor();
void fileConfigSyncLeanState(FileConfig *file_config);
void fileConfigLaunchLeanServer(FileConfig *file_config);



// Load the abbreviation dictionary from the filesystem.
// NOTE: this steals the pointer to `o`.
void load_abbreviation_dict_from_json(AbbreviationDict *dict, json_object *o);

// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_file(AbbreviationDict *dict, fs::path abbrev_path);

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
int suffix_get_unabbrev_len(const char *buf, int finalix, const char *unabbrev, int unabbrevlen);
// return whether there is a suffix of `buf` that looks like `\<unabbrev_prefix>`. 
AbbrevMatchKind suffix_is_unabbrev(const char *buf, int finalix, const char *unabbrev, int unabbrevlen);
const char *abbrev_match_kind_to_str(AbbrevMatchKind);

// return the index of the all matches, for whatever match exists. Sorted to be matches 
// where the match string has the smallest length to the largest length.
// This ensures that the AMK_EXACT_MATCHes will occur at the head of the list.
void abbrev_dict_get_matching_unabbrev_ixs(AbbreviationDict *dict,
  const char *buf, int finalix, std::vector<int> *matchixs);

struct SuffixUnabbrevInfo {
    AbbrevMatchKind kind = AbbrevMatchKind::AMK_NOMATCH;
    int matchlen = -1; // length of the math if kind is not NOMATCH;
    int matchix = -1; // index of the match if kind is not NOMATCH

    // return the information of having no match.
    static SuffixUnabbrevInfo nomatch() {
      return SuffixUnabbrevInfo();
    }
};

// return the best unabbreviation for the suffix of `buf`.
SuffixUnabbrevInfo abbrev_dict_get_unabbrev(AbbreviationDict *dict, const char *buf, int finalix);

// get the path to the executable, so we can build the path to resources.
fs::path get_executable_path();
// get the path to `/path/to/exe/abbreviations.json`.
fs::path get_abbreviations_dict_path();


// return the length (in _buf) of the next code point.
static int utf8_next_code_point_len(const char *str) {
  assert(str);
  char c = str[0];
  const bool b7 = c & (1 << 7);
  const bool b6 = c & (1 << 6);
  const bool b5 = c & (1 << 5);
  const bool b4 = c & (1 << 4);
  const bool b3 = c & (1 << 3);
  if (b7 == 0) {
    return 1;
  } else if (b7 == 1 && b6 == 1 && b5 == 0) {
    return 2;
  } else if (b7 == 1 && b6 == 1 && b5 == 1 &&  b4 == 0) {
    return 3;
  } else if (b7 == 1 && b6 == 1 && b5 == 1 &&  b4 == 1 && b3 == 0) {
    return 4;
  } else {
    assert(false && "unknown UTF-8 width");
  }
};

// return the pointer to the next code point.
// Precondition: assumes that `str` is already code_point aligned.
static const char *utf8_next_code_point(const char *str) {
  return str + utf8_next_code_point_len(str); 
}

// return the length (in _buf) of the previous code point at index `ix`.
// If incomplete, then returns `0`. 
// byte1    | byte2    |  byte3   | byte4    |
// 0xxxxxxx |          |          |          | 
// 110xxxxx | 10xxxxxx |          |          |
// 1110xxxx | 10xxxxxx | 10xxxxxx |
// 11110xxx | 10xxxxxx | 10xxxxxx | 10xxxxxx
static int utf8_prev_code_point_len(const char *str, int ix) {
  assert (ix >= 0);
  int c0 = str[ix];
  const bool b0_7 = c0 & (1 << 7);
  const bool b0_6 = c0 & (1 << 6);
  if (b0_7 == 0) {
    // 0xxxxxxx
    // ^~~~~ix
    return 1; 
  }
  assert(b0_7);
  assert(b0_6 == 0);
  // 10xxxxxx
  // ^~~~~ix

  // ### it is at least 2 characters long.
  assert (ix >= 1);
  unsigned char c1 = str[ix-1];
  const bool b1_7 = c1 & (1 << 7);
  const bool b1_6 = c1 & (1 << 6);
  const bool b1_5 = c1 & (1 << 5);
  assert(b1_7);  
  // 1xxxxxxxx; 10xxxxxx
  //            ^~~~~ix
  if (b1_6) {
    // 11xxxxxx ; 10xxxxxx
    //            ^~~~~ix
    assert(b1_5 == 0);
    // 110xxxxx ; 10xxxxxx
    //            ^~~~~ix
    return 2;
  }
  assert(b1_6 == 0);
  // 10xxxxxxx; 10xxxxxx
  //            ^~~~~ix

  // ### it is at least 3 characters long.
  assert (ix >= 2);
  unsigned char c2 = str[ix-2];
  const bool b2_7 = c2 & (1 << 7);
  const bool b2_6 = c2 & (1 << 6);
  const bool b2_5 = c2 & (1 << 5);
  const bool b2_4 = c2 & (1 << 4);
  const bool b2_3 = c2 & (1 << 3);
  assert(b2_7);  
  // 1xxxxxx; 10xxxxxxx; 10xxxxxx
  //                     ^~~~~ix
  if (b2_6) {
    assert(b2_5);
    assert(b2_4 == 0);
    // 1110xxxx; 10xxxxxxx; 10xxxxxx
    //                      ^~~~~ix
    return 3;
  }
  assert(b1_6 == 0);
  // 10xxxxx; 10xxxxxxx; 10xxxxxx
  //                     ^~~~~ix
  
  // ### it must be 4 characters long.
  assert(ix >= 3);
  unsigned char c3 = str[ix-3];
  const bool b3_7 = c3 & (1 << 7);
  const bool b3_6 = c3 & (1 << 6);
  const bool b3_5 = c3 & (1 << 5);
  const bool b3_4 = c3 & (1 << 4);
  const bool b3_3 = c3 & (1 << 3);
  assert(b3_7);  
  assert(b3_6);  
  assert(b3_5);  
  assert(b3_4);  
  assert(b3_3 == 0);  
  // 11110xxx; 10xxxxx; 10xxxxxxx; 10xxxxxx
  //                                ^~~~~ix
  return 4;
};

