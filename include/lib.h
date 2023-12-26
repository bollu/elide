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
#include <unordered_map>
#include "lean_lsp.h"

struct abuf {
  char *b = nullptr;
  int len = 0;
  abuf() {}

  void appendbuf(const char *s, int slen) {
    assert(slen >= 0 && "negative length!");
    if (slen == 0) { return; }
    this->b = (char *)realloc(b, len + slen);
    assert(this->b && "unable to append string");
    memcpy(this->b + len, s, slen);
    this->len += slen;
  }

  // take the substring of [start,start+len) and convert it to a string.
  // pointer returned must be free.
  char *to_string_start_len(int start, int slen) {
    slen = std::max<int>(0, std::min<int>(slen, len - start));
    assert(slen >= 0);
    char *out = (char *)calloc(slen + 1, sizeof(char));
    if (b != NULL) {
      memcpy(out, b + start, slen);
    }
    return out;
  }

  // take substring [start, buflen).
  char *to_string_from_start_ix(int startix) {
    return to_string_start_len(startix, this->len);
  }

  // take substring [0, slen)
  char *to_string_len(int slen) {
    return to_string_start_len(0, slen);
  }

  // convert buffer to string.
  char *to_string() {
    return to_string_start_len(0, this->len);
  }

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int find_sub_buf(const char *findbuf, int findbuf_len, int begin_ix) {
    for (int i = begin_ix; i < this->len; ++i) {
      int match_len = 0;
      while (i + match_len < this->len && match_len < findbuf_len) {
        if (this->b[i + match_len] == findbuf[match_len]) {
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
  int find_substr(const char *findstr, int begin_ix) {
    return find_sub_buf(findstr, strlen(findstr), begin_ix);
  }

  // Return first index `i >= begin_ix` such that `buf[i] = c`.
  // Return `-1` otherwise.
  int find_char(char c, int begin_ix) { return find_sub_buf(&c, 1, begin_ix); }


  // append a string onto this string.
  void appendstr(const char *s) { appendbuf(s, strlen(s)); }

  // append a format string onto this string. Truncate to length 'len'.
  void appendfmtstr(int len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt); 
    char *buf = (char*)malloc(sizeof(char) * len);
    vsnprintf(buf, len, fmt, args);
    va_end(args);
    appendstr(buf);
    free(buf); 
  }


  // drop a prefix of the buffer. If `drop_len = 0`, then this operation is a
  // no-op.
  void drop_prefix(int drop_len) {
    char *bnew = (char *)malloc(sizeof(char) * (len - drop_len));
    memcpy(bnew, this->b + drop_len, this->len - drop_len);
    free(this->b);
    this->b = bnew;
    this->len = len - drop_len;
  }

  ~abuf() { free(b); }
};

struct LeanServerCursorInfo {
  const char *file_path;
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
  std::vector<json_object *> unhandled_server_requests;

  // low-level API to write strings directly.
  int _write_str_to_child(const char *buf, int len) const;
  // low-level API: read from stdout and write into buffer
  // 'child_stdout_buffer'.
  int _read_stdout_str_from_child_nonblocking();
  int _read_stderr_str_from_child_blocking();
  // tries to read the next JSON record from the buffer, in a nonblocking fashion.
  // If insufficied data is in the buffer, then return NULL.
  json_object *_read_next_json_record_from_buffer_nonblocking();
  // read the next json record from the buffer, and if the buffer is not full,
  // then read in more data until successful read. Will either hang indefinitely
  // or return a `json_object*`. Will never return NULL.
  json_object *_read_next_json_record_from_buffer_blocking();

  // high level APIs to write strutured requests and read responses.
  // write a request, and return the request sequence number.
  LspRequestId write_request_to_child_blocking(const char *method, json_object *params);
  void write_notification_to_child_blocking(const char *method,
                                            json_object *params);
  json_object *read_json_response_from_child_blocking(LspRequestId request_id);

  // high level APIs
  void get_tactic_mode_goal_state(LeanServerState state,
                                  LeanServerCursorInfo cinfo);
  void get_term_mode_goal_state(LeanServerState state,
                                LeanServerCursorInfo cinfo);
  void get_completion_at_point(LeanServerState state,
                               LeanServerCursorInfo cinfo);

  static LeanServerState init(const char *file_path);

private:
};

static const int NSPACES_PER_TAB = 2;
static const char *VERSION = "0.0.1";

enum VimMode {
  VM_NORMAL, // mode where code is only viewed and locked for editing.
  VM_INSERT, // mode where code is edited.
  VM_INFOVIEW_DISPLAY_GOAL, // mode where infoview is shown.
};

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  DEL_CHAR,
};

struct FileRow;


struct Cursor {
  int col = 0; // number of graphemes to move past from the start of the row to get to the current one.
  int row = 0; // index of row. Must be within [0, file->nrows].
};


struct FileConfig {
  bool is_dirty = true; // have not synchronized state with Lean server.
  bool is_initialized = false;
  Cursor cursor;

  // cache of row[cursor.row].cxToRx(cursor.col)
  int cursor_render_col = 0;
  
  // total number of rows.  
  FileRow *row;
  int numrows = 0;
    
  // offset for scrolling.
  int scroll_row_offset = 0;
  // column offset for scrolling. will render from col_offset till endof row.
  int scroll_col_offset = 0;
  
  char *absolute_filepath = nullptr;

  // lean server for file.
  LeanServerState lean_server_state;

  // TextDocument for LSP
  TextDocumentItem text_document_item;

  // plan goal object.
  json_object *leanInfoViewPlainGoal = nullptr;
  json_object *leanInfoViewPlainTermGoal = nullptr;

  void makeDirty() {
    leanInfoViewPlainGoal = nullptr;
    is_dirty = true;
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



struct EditorConfig {
  VimMode vim_mode = VM_NORMAL;
  struct termios orig_termios;
  int screenrows = 0;
  int screencols = 0;

  char statusmsg[80];
  time_t statusmsg_time = 0;

  FileConfig curFile;

  AbbreviationDict abbrevDict;
  EditorConfig() { statusmsg[0] = '\0'; }

};

extern EditorConfig g_editor; // global editor handle.

class FileRow2 {
  char *mem = nullptr; // raw memory.
  int mem_size = 0; // size of memory bank.
};


struct FileRow {
  int size = 0;
  char *chars = nullptr;
  int rsize = 0;
  char *render = nullptr; // convert e.g. characters like `\t` to two spaces.

  int rxToCx(int rx) const {
    int cur_rx = 0;
    for (int cx = 0; cx < this->size; cx++) {
      if (this->chars[cx] == '\t') {
        cur_rx += (NSPACES_PER_TAB - 1) - (cur_rx % NSPACES_PER_TAB);
      }
      cur_rx++;
      if (cur_rx > rx) {
        return cx;
      }
    }
    assert(false && "rx value that is out of range!");
    // return cx;
  }

  int cxToRx(int cx) const {
    int rx = 0;
    for (int j = 0; j < cx && j < this->size; ++j) {
      if (chars[j] == '\t') {
        rx += NSPACES_PER_TAB - (rx % NSPACES_PER_TAB);
      } else {
        rx++;
      }
    }
    return rx;
  }
  // should be private? since it updates info cache.
  void update(FileConfig &E) {

    int ntabs = 0;
    for (int j = 0; j < size; ++j) {
      ntabs += chars[j] == '\t';
    }

    free(render);
    render = (char *)malloc(size + ntabs * NSPACES_PER_TAB + 1);
    int ix = 0;
    for (int j = 0; j < size; ++j) {
      if (chars[j] == '\t') {
        render[ix++] = ' ';
        while (ix % NSPACES_PER_TAB != 0) {
          render[ix++] = ' ';
        }
      } else {
        render[ix++] = chars[j];
      }
    }
    render[ix] = '\0';
    rsize = ix;
    E.is_dirty = true;
  }

  void insertChar(int at, int c, FileConfig &E) {
    assert(at >= 0);
    assert(at <= size);
    // +2, one for new char, one for null.
    chars = (char *)realloc(chars, size + 2);
    // nchars: [chars+at...chars+size)
    // TODO: why +1 at end?
    // memmove(chars + at + 1, chars + at, size - at + 1);
    memmove(chars + at + 1, chars + at, size - at);
    size++;
    chars[at] = c;
    this->update(E);
    E.is_dirty = true;
  }

  void insertString(int at, const char *s, size_t len,  FileConfig &E) {
    // TODO: think of this very carefully.
    // TODO: understand how to handle unicode editing!
    assert(at >= 0);
    assert(at <= size);
    assert(len >= 0);

    size += len;
    chars = (char *)realloc(chars, this->size);

    for(int i = this->size - 1; i >= at + len; --i) {
      assert (i - len >= 0);
      assert(i >= 0);
      assert(i < this->size);
      assert(i - len < this->size);
      chars[i] = chars[i - len];
    }

    // copy string.
    for(int i = 0; i < len; ++i) {
      chars[at + i] = s[i]; 
    }

    this->update(E);
    E.is_dirty = true;
  }

  void appendString(const char *s, size_t len, FileConfig &E) {
    chars = (char *)realloc(chars, size + len + 1);
    // copy string s into chars.
    memcpy(&chars[size], s, len);
    size += len;
    chars[size] = '\0';
    this->update(E);
    E.is_dirty = true;
  }
};

void enableRawMode();
void disableRawMode();

void write_to_child(const char *buf, int len);
int read_stdout_from_child(const char *buf, int bufsize);
int read_stderr_from_child(const char *buf, int bufsize);
void editorSetStatusMessage(const char *fmt, ...);
char *editorPrompt(const char *prompt);
int clamp(int lo, int val, int hi);


/*** terminal ***/
void die(const char *fmt, ...);
int editorReadKey();
void getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);
void editorInsertRow(int at, const char *s, size_t len);
void editorFreeRow(FileRow *row);
void editorDelRow(int at);
// Delete character at location `at`.
// Invariant: `at in [0, row->size)`.
void editorRowDelChar(FileRow *row, int at); 
bool is_space_or_tab(char c);
void editorInsertNewline();
void editorInsertChar(int c); // 32 bit.
void editorDelChar();
void editorOpen(const char *filename);
char *editorRowsToString(int *buflen);
void editorSave();
void editorFind();
void editorDraw();
void editorScroll();
void editorDrawRows(abuf &ab);
void editorDrawStatusBar(abuf &ab);
void editorDrawMessageBar(abuf &ab);
void editorSetStatusMessage(const char *fmt, ...);
void editorMoveCursor(int key);
void editorProcessKeypress();
char *editorPrompt(const char *prompt);
void initEditor();
void fileConfigSyncLeanState(FileConfig *file_config);
void fileConfigLaunchLeanServer(FileConfig *file_config);



// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_json(AbbreviationDict *dict, json_object *o);

// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_file(AbbreviationDict *dict, const char *abbrev_path);

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
// get the path to the executable, so we can build the path to resources.
char *get_executable_path();
// get the path to `/path/to/exe/abbreviations.json`.
char *get_abbreviations_dict_path();


// return the length (in bytes) of the next code point.
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

// return the length (in bytes) of the previous code point at index `ix`.
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

