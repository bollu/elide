#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include "lib.h"
#include "uri_encode.h"
#include "lean_lsp.h"
#include <algorithm>

// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
// [(<accent>;)?<forground>;<background>;
#define ESCAPE_CODE_DULL "\x1b[90;40m" // briht black foreground, black background
#define ESCAPE_CODE_CURSOR_INSERT "\x1b[30;47m" // black foreground, white background
#define ESCAPE_CODE_CURSOR_NORMAL "\x1b[30;100m" // black foreground, bright black background
#define ESCAPE_CODE_CURSOR_SELECT "\x1b[30;46m" // black foreground, cyan background
#define ESCAPE_CODE_RED "\x1b[30;41m" // black foreground, red background
#define ESCAPE_CODE_YELLOW "\x1b[30;43m" // black foreground, yellow background
#define ESCAPE_CODE_GREEN "\x1b[30;42m" // black foreground, green background
#define ESCAPE_CODE_BLUE "\x1b[30;44m" // black foreground, blue background
#define ESCAPE_CODE_UNSET "\x1b[0m"

#define CHECK_POSIX_CALL_0(x) { do { int success = x == 0; if(!success) { perror("POSIX call failed"); }; assert(success); } while(0); }
// check that minus 1 is notreturned.
#define CHECK_POSIX_CALL_M1(x) { do { int fail = x == -1; if(fail) { perror("POSIX call failed"); }; assert(!fail); } while(0); }


void disableRawMode();
void enableRawMode();

/** terminal rendering utils for consistent styling. **/

// draw the text 'codepoint', plus the cursor backgrounding in text area mode 'tam' into
// abuf 'dst'.
void abufAppendCodepointWithCursor(abuf *dst, TextAreaMode tam, const char *codepoint) {
  if (tam == TAM_Normal) {
    dst->appendstr(ESCAPE_CODE_CURSOR_NORMAL);
  } else {
    assert(tam == TAM_Insert);
    dst->appendstr(ESCAPE_CODE_CURSOR_INSERT);
  }
  dst->appendCodepoint(codepoint); // draw the text.
  dst->appendstr(ESCAPE_CODE_UNSET);
}


// draw the text + cursor at position cursorIx into abuf 'dst'.
//   dst: destination buffer to append into.
//   row : the row to be drawn. 
//   colIx : index of the column to be drawn. `0 <= colIx <= row.ncodepoints()`.
//   cursorIx : index of cursor, of value `0 <= cursorIx <= row.ncodepoints()`.
//   tam : text area mode, affects if the cursor will be bright or dull.
void appendColWithCursor(abuf *dst, const abuf *row, 
  Size<Codepoint> colIx, 
  Size<Codepoint> cursorIx, 
  TextAreaMode tam) {
  assert (cursorIx <= row->ncodepoints());
  assert (colIx <= row->ncodepoints());
  const bool cursorAtCol = colIx == cursorIx;

  if (colIx < row->ncodepoints()) {
    const char *codepoint = row->getCodepoint(colIx.toIx());
    if (cursorAtCol) {
      abufAppendCodepointWithCursor(dst, tam, codepoint);
    } else {
      dst->appendCodepoint(codepoint);
    }
  } else {
    assert(colIx == row->ncodepoints());
    if (cursorAtCol) {
      abufAppendCodepointWithCursor(dst, tam, " ");
    }
    // nothing to render here, just a blank space. 
  }
}



/*** lean server state ***/
// quick refresher on how pipes work:
// 1. pipe(fds) creates a pipe, where fds[0] is the read fd, fds[1] is the write fd.
//   one can only write to fds[1], and one can only read from fds[0].
// 2. dup2(x, y) ties the two file descriptors together, making a write to one equal to writing to the other.
// 2a. If we want to write into the STDIN of a process via the output of a pipe, we would use
//      `dup2(STDIN_FILENO, fds[PIPE_READ_IX])`;
// 3. for cleanliness, we close() the sides of the pipes that must be left unsused on each side.
//    so on the producer side `write(fds[1], data)`, we close the consumer fd `close(fds[0])` and vice versa.
// we search the directory tree of 'file_path'. If we find a `lakefile.lean`, then we call `lake --serve`
// at that cwd. Otherwise, we call `lean --server` at the file working directory.
// NOTE: the pointer `lakefile_dirpath` is consumed.
void _exec_lean_server_on_child(std::optional<fs::path> lakefile_dirpath) {
  int process_error = 0;
  // no lakefile.
  if (!lakefile_dirpath) {
    fprintf(stderr, "starting 'lean --server'...\n");
    const char *process_name = "lean";
    char * const argv[] = { strdup(process_name), strdup("--server"), NULL };
    process_error = execvp(process_name, argv);
  } else {
    std::error_code ec;
    fs::current_path(*lakefile_dirpath, ec);
    if (ec) {
      die("ERROR: unable to switch to 'lakefile.lean' directory");
    }; 
    const char * process_name = "lake";
    char * const argv[] = { strdup(process_name), strdup("serve"), NULL };
    process_error = execvp(process_name, argv);
  }

  if (process_error == -1) {
    perror("failed to launch lean server");
    abort();
  }
}

// tries to find the lake for `file_path`,
// and returns NULL if file_path is NULL.
std::optional<fs::path> getFilePathAmongstParents(fs::path dirpath, const char *needle) {
  std::cerr << "dirpath: '" << dirpath << "'\n";
  assert(dirpath.is_absolute());

  // only iterate this loop a bounded number of times.
  int NITERS = 1000;
  for(int i = 0; i < NITERS; ++i) {
    assert(i != NITERS - 1 && 
      "ERROR: recursing when walking up parents to find `lakefile.lean`.");
    for (auto const &it : fs::directory_iterator{dirpath}) {
      if (it.path().filename() == needle) {
        return it.path();
      }
    }
    fs::path dirpath_parent = dirpath.parent_path();
    // we hit the root. 
    if (dirpath_parent == dirpath) { break; }
    dirpath = dirpath_parent;
  }
  return {};
}

// create a new lean server.
// if file_path == NULL, then create `lean --server`.
LeanServerState LeanServerState::init(std::optional<fs::path> absolute_filepath) {
  LeanServerState state;

  CHECK_POSIX_CALL_0(pipe(state.parent_buffer_to_child_stdin));
  CHECK_POSIX_CALL_0(pipe2(state.child_stdout_to_parent_buffer, O_NONBLOCK));
  CHECK_POSIX_CALL_0(pipe(state.child_stderr_to_parent_buffer));

  // open debug logging files.
  state.child_stdin_log_file = fopen("/tmp/edtr-child-stdin", "a+");
  state.child_stdout_log_file = fopen("/tmp/edtr-child-stdout", "a+");
  state.child_stderr_log_file = fopen("/tmp/edtr-child-stderr", "a+");

  fputs("\n===\n", state.child_stdin_log_file);  
  fputs("\n===\n", state.child_stdout_log_file);
  fputs("\n===\n", state.child_stderr_log_file);

  if (absolute_filepath) {
    assert(absolute_filepath->is_absolute());
    state.lakefile_dirpath = ([&absolute_filepath]() -> std::optional<fs::path> {
      std::optional<fs::path> p = 
      	getFilePathAmongstParents(absolute_filepath->remove_filename(), "lakefile.lean"); 
      if (p) { return p->remove_filename(); }
      else { return {}; }
    })();
    std::cerr << "lakefile_dirpath: " << (state.lakefile_dirpath ? state.lakefile_dirpath->string() : "NO LAKEFILE") << "\n";
    if (state.lakefile_dirpath) {
      assert(state.lakefile_dirpath->is_absolute());
    }
  }

  pid_t childpid = fork();
  if(childpid == -1) {
    perror("ERROR: fork failed.");
    exit(1);
  };

  if(childpid == 0) {
    // child->parent, child will only write to this pipe, so close read end.
    close(state.child_stdout_to_parent_buffer[PIPE_READ_IX]);
    // child->parent, child will only write to this pipe, so close read end.
    close(state.child_stderr_to_parent_buffer[PIPE_READ_IX]);

    // parent->child, child will only read from this end, so close write end.
    close(state.parent_buffer_to_child_stdin[PIPE_WRITE_IX]);

    // it is only legal to call `write()` on stdout. So we tie the `PIPE_WRITE_IX` to `STDOUT`
    dup2(state.child_stderr_to_parent_buffer[PIPE_WRITE_IX], STDERR_FILENO);
    // it is only legal to call `write()` on stdout. So we tie the `PIPE_WRITE_IX` to `STDOUT`
    dup2(state.child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO);
    // it is only legal to call `read()` on stdin. So we tie the `PIPE_READ_IX` to `STDIN`
    dup2(state.parent_buffer_to_child_stdin[PIPE_READ_IX], STDIN_FILENO);
    // execute lakefile. 
    _exec_lean_server_on_child(state.lakefile_dirpath);
  } else {
    // parent will only write into this pipe, so close the read end.
    close(state.parent_buffer_to_child_stdin[PIPE_READ_IX]);
    // parent will only read from this pipe so close the wite end.
    close(state.child_stdout_to_parent_buffer[PIPE_WRITE_IX]);
    // parent will only read from this pipe so close the wite end.
    close(state.child_stderr_to_parent_buffer[PIPE_WRITE_IX]);

  }
  // return lean server state to the parent process.
  state.initialized = true; 
  return state;
};


struct LspResponse {
  int content_length = -1;
  json_object *response_obj = NULL;
};

int LeanServerState::_write_str_to_child(const char *buf, int len) const {
  int nwritten = write(this->parent_buffer_to_child_stdin[PIPE_WRITE_IX], buf, len);

  (void)fwrite(buf, len, 1, this->child_stdin_log_file);
  fflush(this->child_stdin_log_file);
  return nwritten;
};

int LeanServerState::_read_stdout_str_from_child_nonblocking() {
  const int BUFSIZE = 4096;
  char buf[BUFSIZE];
  int nread = read(this->child_stdout_to_parent_buffer[PIPE_READ_IX], buf, BUFSIZE);
  if (nread == -1) {

    if (errno == EAGAIN) {
      // EAGAIN = non-blocking I/O, there was no data ro read.
      return 0;
    }
    die("unable to read from stdout of child lean server");
  }
  this->child_stdout_buffer.appendbuf(buf, nread);

  (void)fwrite(buf, nread, 1, this->child_stdout_log_file);
  fflush(this->child_stdout_log_file);
  return nread;
};

int LeanServerState::_read_stderr_str_from_child_blocking() {
  const int BUFSIZE = 4096;
  char buf[BUFSIZE];
  int nread = read(this->child_stderr_to_parent_buffer[PIPE_READ_IX], buf, BUFSIZE);
  this->child_stderr_buffer.appendbuf(buf, nread);

  (void)fwrite(buf, nread, 1, this->child_stderr_log_file);
  fflush(this->child_stderr_log_file);
  return nread;
};

LspRequestId LeanServerState::write_request_to_child_blocking(const char * method, json_object *params) {
  const int id = this->next_request_id++;
json_object *o = json_object_new_object();
  json_object_object_add(o, "jsonrpc", json_object_new_string("2.0"));
  json_object_object_add(o, "id", json_object_new_int(id));
  json_object_object_add(o, "method", json_object_new_string(method));
  if (params) {
    json_object_object_add(o, "params", params);
  }

  const char *obj_str = json_object_to_json_string(o);
  const int obj_strlen = strlen(obj_str);


  char *request_str = (char*)calloc(sizeof(char), obj_strlen + 128);
  const int req_len = sprintf(request_str, "Content-Length: %d\r\n\r\n%s", obj_strlen, obj_str);
  this->_write_str_to_child(request_str, req_len);
  free(request_str);
  json_object_put(o);
  return LspRequestId(id);
}

void LeanServerState::write_notification_to_child_blocking(const char * method, json_object *params) {
  json_object *o = json_object_new_object();
  json_object_object_add(o, "jsonrpc", json_object_new_string("2.0"));
  json_object_object_add(o, "method", json_object_new_string(method));
  if (params) {
    json_object_object_add(o, "params", params);
  }

  const char *obj_str = json_object_to_json_string(o);
  const int obj_strlen = strlen(obj_str);


  char *request_str = (char*)calloc(sizeof(char), obj_strlen + 128);
  const int req_len = sprintf(request_str, "Content-Length: %d\r\n\r\n%s", obj_strlen, obj_str);
  this->_write_str_to_child(request_str, req_len);
  json_object_put(o);
  free(request_str);
}

// tries to read the next JSON record from the buffer, in a nonblocking fashion.
json_object_ptr LeanServerState::_read_next_json_record_from_buffer_nonblocking() {
  this->_read_stdout_str_from_child_nonblocking();
  const char *CONTENT_LENGTH_STR = "Content-Length:";
  const char *DOUBLE_NEWLINE_STR = "\r\n\r\n";


  const int content_length_begin_ix = child_stdout_buffer.find_substr(CONTENT_LENGTH_STR, 0);
  const int header_line_begin_ix = child_stdout_buffer.find_substr(DOUBLE_NEWLINE_STR, 0);


  if (header_line_begin_ix == -1) { return NULL; }
  assert (content_length_begin_ix != -1);

  // yay, found content length.
  int content_length = atoi(child_stdout_buffer.buf() + content_length_begin_ix + strlen(CONTENT_LENGTH_STR));

  // we don't have enough data in the buffer to read the content length
  const int header_line_end_ix = header_line_begin_ix + strlen(DOUBLE_NEWLINE_STR);
  if (child_stdout_buffer.len() < header_line_end_ix + content_length) {
    return NULL;
  }

  // parse.
  json_tokener *tok = json_tokener_new(); // TODO: do not create/destroy this object each time.
  json_object *o = json_tokener_parse_ex(tok,
    child_stdout_buffer.buf() + header_line_end_ix, content_length);
  assert(o != NULL);
  json_tokener_free(tok);

  // drop the response from the buffer, now that we have correctly consumes the json request.
  child_stdout_buffer.dropNBytesMut(header_line_end_ix + content_length);

  return o;
}

void LeanServerState::tick_nonblocking() {
  // TODO: convert to std::optional.
  json_object_ptr o_optional = _read_next_json_record_from_buffer_nonblocking();
  if (o_optional == NULL) { return; }
  json_object *response_ido = NULL;
  if (json_object_object_get_ex(o_optional, "id", &response_ido) && 
      json_object_get_type(response_ido) == json_type_int) {
      const int response_id = json_object_get_int(response_ido);
      auto it = this->request2response.find(response_id);
      assert(it == this->request2response.end());
      this->request2response[response_id] = o_optional;
      this->nresponses_read++;
  } else {
      this->unhandled_server_requests.push_back(o_optional);
  }
}

std::optional<json_object_ptr> LeanServerState::read_json_response_from_child_nonblocking(LspRequestId request_id) {
  this->tick_nonblocking();
  auto it = this->request2response.find(request_id);
  if (it == this->request2response.end()) {
    return {};
  }
  assert(it->second != NULL);
  return std::optional(it->second);
}


/*** data ***/
EditorConfig g_editor;

/*** terminal ***/

void die(const char *fmt, ...) {

  va_list args;
  va_start(args, fmt);
  const int ERROR_LEN = 9000; 
  char *buf = (char*)malloc(sizeof(char) * ERROR_LEN);
  vsnprintf(buf, ERROR_LEN, fmt, args);
  va_end(args);
  perror(buf);
  exit(1);
}

void disableRawMode() {
  // show hidden cursor
  const char *showCursor = "\x1b[?25h";
  // no point catching errors at this state, we are closing soon anyway.
  int _ = write(STDOUT_FILENO, showCursor, strlen(showCursor));
  (void)_;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_editor.orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &g_editor.orig_termios) == -1) {
    die("tcgetattr");
  };
  atexit(disableRawMode);

  struct termios raw = g_editor.orig_termios;
  int lflags = 0;
  lflags |= ECHO;   // don't echo back.
  lflags |= ICANON; // read byte at a time.
  lflags |= ISIG;   // no C-c, C-z for me!
  lflags |= IEXTEN; // Disables C-v for waiting for another character to be sent
                    // literally.
  raw.c_lflag &= ~(lflags);

  // | disable flow control. C-s/C-q to stop sending and start sending data.
  int iflags = 0;
  iflags |= (IXON);
  // disable translating C-m (\r) into \n. (Input-Carriage-Return-NewLine)
  iflags |= ICRNL;
  // INPCK enables parity checking, which doesn’t seem to apply to modern
  // terminal emulators. ISTRIP causes the 8th bit of each input byte to be
  // stripped, meaning it will set it to 0. This is probably already turned off.
  // When BRKINT is turned on, a break condition will cause a SIGINT signal to
  // be sent to the program, like pressing Ctrl-C.
  iflags |= BRKINT | INPCK | ISTRIP;
  raw.c_iflag &= ~iflags;

  // disable all output processing of output.
  raw.c_oflag &= ~(OPOST);

  raw.c_cflag |= (CS8); // This specifies eight bits per byte.

  // | min # of bytes to be read before read() return
  raw.c_cc[VMIN] = 0;
  // | max amount of time to wait before read() return.
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

void getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  // n: terminal status | 6: cursor position
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    die(" | unable to read terminal status");
  }

  // Then we can read the reply from the standard input.
  // The reply is an escape sequence!
  // It’s an escape character ( 27 ), followed by a [
  // character, and then the actual response: 24;80R , or similar.
  //(This escape sequence is documented as Cursor Position Report.)
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  assert(buf[0] == '\x1b');
  assert(buf[1] == '[');

  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
    die("unable to parse cursor string");
  };

}

int getWindowSize(int *rows, int *cols) {

  // getCursorPosition(rows, cols);

  struct winsize ws;
  // TIOCGWINSZ: terminal IOctl get window size.
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    die("unable to get window size");
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    // fprintf(stderr, "cols: %d | rows: %d\n", *cols, *rows);
    // die("foo");
  }
  return 0;
}

/*** row operations ***/

// insert a new row at location `at`, and store `s` at that row.
// so rows'[at] = <new str>, rows'[at+k] = rows[at + k - 1];
// This also copies the indentation from the previous line into the new line.
void fileConfigInsertRowBefore(FileConfig *f, int at, const char *s, size_t len) {
  // assert (at >= 0);
  // assert(at <= f->rows.size()); 

  if (at < 0 || at > f->rows.size()) {
    return;
  }

  f->rows.push_back(abuf()); 
  for(int i = f->rows.size() - 1; i >= at + 1; i--)  {
    f->rows[i] = f->rows[i - 1];   
  }
  f->rows[at].setBytes(s, len);
  f->makeDirty();
}

// delete the current row.
// TODO: this needs lots of testing!
void fileConfigDeleteCurrentRow(FileConfig *f) {
  if (f->cursor.row < f->rows.size()) {
    for(int i = f->cursor.row; i < f->rows.size() - 1; i++)  {
      f->rows[i] = f->rows[i + 1];   
    }
    f->rows.pop_back();
  }
  if (f->cursor.row == f->rows.size()) {
    f->cursor.col = Size<Codepoint>(0);
  } else {
    f->cursor.col = 
      std::min<Size<Codepoint>>(f->cursor.col,
        f->rows[f->cursor.row].ncodepoints());
  }
}



void fileConfigDelRow(FileConfig *f, int at) {
  f->makeDirty();
  if (at < 0 || at >= f->rows.size()) {
    return;
  }

  for(int i = at; i < f->rows.size() - 1; ++i) {
    f->rows[i] = f->rows[i+1];
  }
  f->rows.pop_back(); // drop last element.
}


bool is_space_or_tab(char c) {
  return c == ' ' || c == '\t';
}

/*** editor operations ***/
void fileConfigInsertEnterKey(FileConfig *f) {
  f->makeDirty();
  if (f->cursor.col == Size<Codepoint>(0)) {
    // at first column, insert new row.
    fileConfigInsertRowBefore(f, f->cursor.row, "", 0);
    // place cursor at next row (f->cursor.row + 1), first column (cx=0)
    f->cursor.row++;
    f->cursor.col = Size<Codepoint>(0);
  } else {
    // at column other than first, so chop row and insert new row.
    abuf *row = &f->rows[f->cursor.row];
    // TODO: simplify code by using `abuf`.
    // legal previous row, copy the indentation.
    // note that the checks 'num_indent < row.size' and 'num_indent < f->cursor.col' are *not* redundant.
    // We only want to copy as much indent exists upto the cursor.

    abuf new_row_contents;
    // this is legal because space/tab is both a codepoint and an Ix, they are commensurate.
    Size<Codepoint> num_indent(0);
    for(Ix<Codepoint> i(0); i < row->ncodepoints() && i < f->cursor.col; i++)  {
      const char *codepoint = row->getCodepoint(i); 
      if (is_space_or_tab(*codepoint)) {
        num_indent++; // keep track of how many indents we have added, so we can adjust the column. 
        new_row_contents.appendCodepoint(codepoint);
      } else {
        break;
      }
    }

    // add all code point size from col till end.
    for(Ix<Codepoint> i(f->cursor.col.toIx()); i < row->ncodepoints(); ++i) {
      new_row_contents.appendCodepoint(row->getCodepoint(i));
    }

    // create a row at f->cursor.row + 1 containing data;
    fileConfigInsertRowBefore(f, f->cursor.row + 1, new_row_contents.buf(), new_row_contents.len());

    // pointer invalidated, get pointer to current row again,
    row = &f->rows[f->cursor.row];
    // chop off at row[...:f->cursor.col]
    row->truncateNCodepoints(Size<Codepoint>(f->cursor.col));
    f->makeDirty();
    // place cursor at next row (f->cursor.row + 1), column of the indent.
    f->cursor.row++;
    f->cursor.col = num_indent;
  }
}

void fileConfigInsertCharBeforeCursor(FileConfig *f, int c) {
  f->makeDirty();

  if (f->cursor.row == f->rows.size()) {
    f->mkUndoMemento();
    fileConfigInsertRowBefore(f, f->rows.size(), "", 0);
  }

  abuf *row = &f->rows[f->cursor.row];

  // if `c` is one of the delinators of unabbrevs.
  if (row->ncodepoints() > Size<Codepoint>(0) && 
      (c == ' ' || c == '\t' || c == '(' || c == ')' || c == '\\' || ispunct(c))) {
    // we are inserting a space, check if we have had a full match, and if so, use it.
    SuffixUnabbrevInfo info = abbrev_dict_get_unabbrev(&g_editor.abbrevDict, 
      row->getCodepoint(Ix<Codepoint>(0)), // this is a hack to get the buffer...
      row->getBytesTill(f->cursor.col).size - 1);
    if (info.kind == AMK_EXACT_MATCH) {
      assert(f->cursor.col.size > 0);

      // assumption: the unabbrev is pure ASCII. Need to delete `matchlen + 1` to kill
      // the full string, plus the backslash.
      for(int i = 0; i < info.matchlen + 1; ++i) {
        assert((g_editor.abbrevDict.unabbrevs[info.matchix][i] & (1 << 7)) == 0); // ASCII;
        row->delCodepointAt(Ix<Codepoint>(f->cursor.col.largestIx()));
        f->cursor.col--;
      }

      // TODO: check if we have `toInserts` that are more than 1 codepoint.
      const char *toInsert = g_editor.abbrevDict.abbrevs[info.matchix];
        row->insertCodepointBefore(f->cursor.col, toInsert);
      f->cursor.col++;
    }
  }
  row->insertByte(f->cursor.col, c);
  f->cursor.col++; // move cursor.
}

// TODO: come up with the correct abstractions for these.
// run the X command in vim.
void fileConfigXCommand(FileConfig *f) {
  f->makeDirty();
  if (f->cursor.row == f->rows.size()) { return; }

  abuf *row = &f->rows[f->cursor.row];

  // nothing under cursor.
  if (f->cursor.col == row->ncodepoints()) { return; }
  // delete under the cursor.
  row->delCodepointAt(f->cursor.col.toIx());
  f->makeDirty();
}

// TODO: study this and make a better abstraction for the cursor location.
void fileConfigBackspace(FileConfig *f) {
  f->makeDirty();
  if (f->cursor.row == f->rows.size()) {
    return;
  }
  if (f->cursor.col == Size<Codepoint>(0) && f->cursor.row == 0) {
    return;
  }

  f->makeDirty();
  abuf *row = &f->rows[f->cursor.row];

  // if col > 0, then delete at cursor. Otherwise, join lines toegether.
  if (f->cursor.col > Size<Codepoint>(0)) {
    // delete at the cursor.
    row->delCodepointAt(f->cursor.col.largestIx());
    f->cursor.col--;
  } else {
    // place cursor at last column of prev row.
    f->cursor.col = f->rows[f->cursor.row - 1].ncodepoints();
    // append string.
    for(Ix<Codepoint> i(0); i < row->ncodepoints(); ++i) {
      f->rows[f->cursor.row - 1].appendCodepoint(row->getCodepoint(i));
    }
    // delete current row
    fileConfigDelRow(f, f->cursor.row);
    // go to previous row.
    f->cursor.row--;
  }
}


// TODO: find some neat way to maintain this state.
// The annoying thing is that this needs to be initialized
// 
void fileConfigLaunchLeanServer(FileConfig *file_config) {
  // TODO: find some neat way to maintain this state.
  assert(file_config->lean_server_state.initialized == false);
  file_config->lean_server_state = LeanServerState::init(file_config->absolute_filepath); // start lean --server.  

  json_object *req = lspCreateInitializeRequest();
  LspRequestId request_id = 
    file_config->lean_server_state.write_request_to_child_blocking("initialize", req);

  // busy wait.
  // TODO: cleanup the busy wait for LSP state.
  const int NBUSYROUNDS = 5;
  std::optional<json_object_ptr> response;
  for(int i = 0; i < NBUSYROUNDS; ++i) {
    file_config->lean_server_state.tick_nonblocking();
    response = 
    	file_config->lean_server_state.read_json_response_from_child_nonblocking(request_id);
    if (response) { break; }
    sleep(1);
  }
  assert(bool(response) && "launching lean server");

  // initialize: send initialized
  req = lspCreateInitializedNotification();
  file_config->lean_server_state.write_notification_to_child_blocking("initialized", req);
}


void fileConfigSyncLeanState(FileConfig *file_config) {
  json_object *req = nullptr;
  assert(file_config->is_initialized);
  if (file_config->text_document_item.is_initialized && 
      !file_config->whenDirtyInfoView()) {
    return; // no point syncing state if it isn't dirty, and the state has been initalized before.
  }

  assert(file_config->lean_server_state.initialized == true);
  if (!file_config->text_document_item.is_initialized) {
    file_config->text_document_item.init_from_file_path(file_config->absolute_filepath);
  } else {
    file_config->text_document_item.version += 1;
    free(file_config->text_document_item.text);
    abuf buf;
    fileConfigRowsToBuf(file_config, &buf);
    // assert(buf.len() > 0);
    // assert(buf.buf()[buf.len() - 1] == 0); // must be null-termianted.
    file_config->text_document_item.text = buf.to_string();
  }
  assert(file_config->text_document_item.is_initialized);
  // textDocument/didOpen
  req = lspCreateDidOpenTextDocumentNotifiation(file_config->text_document_item);
  file_config->lean_server_state.write_notification_to_child_blocking("textDocument/didOpen", req);
}

void fileConfigRequestGoalState(FileConfig *file_config) {
  assert(file_config->is_initialized);
  assert(file_config->text_document_item.is_initialized);

  json_object *req = nullptr;
  LspRequestId  request_id;

  // $/lean/plainGoal

  // TODO: need to convert col to 'bytes'
  req = lspCreateLeanPlainGoalRequest(file_config->text_document_item.uri, 
    cursorToLspPosition(file_config->cursor));
  request_id = 
    file_config->lean_server_state.write_request_to_child_blocking("$/lean/plainGoal", req);
  file_config->leanInfoViewPlainGoal = JsonNonblockingResponse(request_id);
  // file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

  // $/lean/plainTermGoal
  req = lspCreateLeanPlainTermGoalRequest(file_config->text_document_item.uri, 
    cursorToLspPosition(file_config->cursor));
  request_id = 
    file_config->lean_server_state.write_request_to_child_blocking("$/lean/plainTermGoal", req);
  file_config->leanInfoViewPlainTermGoal = JsonNonblockingResponse(request_id);
  // file_config->leanInfoViewPlainTermGoal = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

  // textDocument/hover

  req = lspCreateTextDocumentHoverRequest(file_config->text_document_item.uri, 
    cursorToLspPosition(file_config->cursor));
  request_id = file_config->lean_server_state.write_request_to_child_blocking("textDocument/hover", req);
  file_config->leanHoverViewHover = JsonNonblockingResponse(request_id);
  // file_config->leanHoverViewHover = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

}


/*** file i/o ***/
FileConfig::FileConfig(FileLocation loc) {
  this->cursor = loc.cursor;
  this->absolute_filepath = loc.absolute_filepath;
  assert(this->absolute_filepath.is_absolute());
  FILE *fp = fopen(this->absolute_filepath.c_str(), "a+");
  if (!fp) {
    die("fopen: unable to open file '%s'", this->absolute_filepath.c_str());
  }
  fseek(fp, 0, /*whence=*/SEEK_SET);

  char *line = nullptr;
  size_t linecap = 0; // allocate memory for line read.
  ssize_t linelen = -1;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    fileConfigInsertRowBefore(this, this->rows.size(), line, linelen);
  }
  free(line);
  fclose(fp);
  this->is_initialized = true;
}


void fileConfigRowsToBuf(FileConfig *file, abuf *buf) {
  for (int r = 0; r < file->rows.size(); r++) {
    if (r > 0) { buf->appendChar('\n'); }
    buf->appendbuf(file->rows[r].getRawBytesPtrUnsafe(), file->rows[r].nbytes().size);
  }
}

void fileConfigDebugPrint(FileConfig *file, abuf *buf) {
  for (int r = 0; r < file->rows.size(); r++) {
    if (r > 0) { buf->appendChar('\n'); }
    Ix<Codepoint> c(0);
    // buf->appendChar('\'');
    for (; c < file->rows[r].ncodepoints(); c++) {
      if (r == file->cursor.row && c == file->cursor.col.toIx()) {
        buf->appendChar('|');
      }
      // TODO: convert 'buf' API to also use Sizes.
      buf->appendCodepoint(file->rows[r].getCodepoint(c));
    }

    // recall that cursor can occur *after* line end.
    if (r == file->cursor.row && c  == file->cursor.col.toIx()) {
        buf->appendChar('|');
    }
    // buf->appendChar('\'');
  }
}



void fileConfigSave(FileConfig *f) {
  if (!f->whenDirtySave()) { return; }

  abuf buf;
  fileConfigRowsToBuf(f, &buf);
  // | open for read and write
  // | create if does not exist
  // 0644: +r, +w
  int fd = open(f->absolute_filepath.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    // editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
  }
  assert(fd != -1 && "unable to open file");
  // | set file to len.
  int err = ftruncate(fd, buf.len());
  assert(err != -1 && "unable to truncate");
  int nwritten = write(fd, buf.buf(), buf.len());
  assert(nwritten == buf.len() && "wasn't able to write enough bytes");
  // editorSetStatusMessage("Saved file");
  close(fd);
}



LspPosition cursorToLspPosition(const Cursor c) {
  return LspPosition(c.row, c.col.size);
}

Cursor LspPositionToCursor(const LspPosition p) {
  Cursor c;
  c.row = p.row;
  c.col = p.col;
  return c;
}

enum GotoKind {
  Definition,
  TypeDefiition
};

void fileConfigGotoDefinitionNonblocking(FileConfig *file_config, GotoKind kind) {
  assert(file_config->is_initialized);
  assert(file_config->text_document_item.is_initialized);

  json_object *req = 
      lspCreateTextDocumentDefinitionRequest(file_config->text_document_item.uri,
          cursorToLspPosition(file_config->cursor));
  tilde::tildeWrite("Request [textDocument/definition] %s", json_object_to_json_string(req));

  std::string gotoKindStr;
  if (kind == GotoKind::Definition) {
    gotoKindStr = "textDocument/definition";
  } else if (kind == GotoKind::TypeDefiition) {
    gotoKindStr = "textDocument/typeDefinition";
  } else {
    assert(false && "unknown GotoKind");
  }
  assert(gotoKindStr != "");
  file_config->leanGotoRequest.request = file_config->lean_server_state.write_request_to_child_blocking(gotoKindStr.c_str(), req);
  return;

}

/*** append buffer ***/

/*** output ***/


int num_digits(int n) {
  if (n < 0) { n = -n; }
  if (n == 0) { return 1; }
  int ndigits = 0;
  while(n > 0) { n = n / 10; ndigits++; }
  return ndigits;
}

// 0 = ones place, 1 = tens place, and so on.
int get_digit(int n, int ix) {
  for(int i = 0; i < ix; ++i) {
    n = n / 10;
  }
  return n % 10;
}

// write the number 'num' into the string and return the length of the string.
int write_int_to_str(char *s, int num) {
  assert (num >= 0);
  int ndigits = num_digits(num);
  for(int i = 0; i < ndigits; ++i) {
    s[ndigits - 1 - i] = '0' + get_digit(num, i);
  }
  s[ndigits] = 0;
  return ndigits;
}



void fileConfigDraw(FileConfig *f) {
  f->cursor_render_col = 0;
  assert (f->cursor.row >= 0 && f->cursor.row <= f->rows.size());
  if (f->cursor.row < f->rows.size()) {
    f->cursor_render_col = 
      f->rows[f->cursor.row].cxToRx(Size<Codepoint>(f->cursor.col));
  }
  if (f->cursor.row < f->scroll_row_offset) {
    f->scroll_row_offset = f->cursor.row;
  }
  if (f->cursor.row >= f->scroll_row_offset + g_editor.screenrows) {
    f->scroll_row_offset = f->cursor.row - g_editor.screenrows + 1;
  }
  if (f->cursor_render_col < f->scroll_col_offset) {
    f->scroll_col_offset = f->cursor_render_col;
  }
  if (f->cursor_render_col >= f->scroll_col_offset + g_editor.screencols) {
    f->scroll_col_offset = f->cursor_render_col - g_editor.screencols + 1;
  }

  abuf ab;
  ab.appendstr("\x1b[?25l"); // hide cursor

  // VT100 escapes.
  // \x1b: escape.
  // J: erase in display.
  // [2J: clear entire screen
  // trivia: [0J: clear screen from top to cuursor, [1J: clear screen from
  // cursor to bottom
  //          0 is default arg, so [J: clear screen from cursor to bottom
  ab.appendstr("\x1b[2J");

  // H: cursor position
  // [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab.appendstr("\x1b[1;1H");

  // When we print the line number, tilde, we then print a
  // "\r\n" like on any other line, but this causes the terminal to scroll in
  // order to make room for a new, blank line. Let’s make the last line an
  // exception when we print our
  // "\r\n".
  // plus one at the end for the pipe, and +1 on the num_digits so we start from '1'.
  // plus one more for the progress bar character
  const int NCHARS_PIPE = 1;
  const int NCHARS_PROGRESSBAR = 1;
  const int LINE_NUMBER_NUM_CHARS = num_digits(g_editor.screenrows + f->scroll_row_offset + NCHARS_PIPE + NCHARS_PROGRESSBAR) + 1;
  for (int row = 0; row < g_editor.screenrows; row++) {
    int filerow = row + f->scroll_row_offset;

    // convert the line number into a string, and write it.
    {

      char *line_number_str = (char *)calloc(sizeof(char), (LINE_NUMBER_NUM_CHARS + 1)); // TODO: allocate once.
      if (filerow <= f->progressbar.startRow || f->progressbar.finished) {
        ab.appendstr(ESCAPE_CODE_GREEN "▌" ESCAPE_CODE_UNSET);
      } else {
        ab.appendstr(ESCAPE_CODE_YELLOW "▌" ESCAPE_CODE_UNSET);
      }

      bool row_needs_unset = false;
      if (filerow == f->cursor.row) { ab.appendstr(ESCAPE_CODE_CURSOR_SELECT); row_needs_unset = true; }
      // code in view mode is renderered gray
      else if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr(ESCAPE_CODE_DULL); row_needs_unset = true; }

      for(const LspDiagnostic &d : f->lspDiagnostics) {
        if (d.range.start.row >= filerow && d.range.end.row <= filerow) {
	  row_needs_unset = true;
	  if (d.severity == LspDiagnosticSeverity::Error) {
	    ab.appendstr(ESCAPE_CODE_RED);
	  }
	  else if (d.severity == LspDiagnosticSeverity::Warning) {
	    ab.appendstr(ESCAPE_CODE_YELLOW);
	  }
	  else if (d.severity == LspDiagnosticSeverity::Hint || d.severity == LspDiagnosticSeverity::Information) {
	    ab.appendstr(ESCAPE_CODE_BLUE);
	  } else {
	    assert(false && "unhandled severity");
	  }
	}
      }

      int ix = write_int_to_str(line_number_str, filerow + 1);
      while(ix < LINE_NUMBER_NUM_CHARS - 1) {
        line_number_str[ix] = ' ';
        ix++;
      }
      line_number_str[ix] = '|';
      ab.appendstr(line_number_str);
      free(line_number_str);

      // code in view mode is renderered gray, so reset.
      if (row_needs_unset) { ab.appendstr(ESCAPE_CODE_UNSET); }

    }
    // code in view mode is renderered gray
    if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr("\x1b[37;40m"); }

    const TextAreaMode textAreaMode = 
      g_editor.vim_mode == VM_NORMAL ? TextAreaMode::TAM_Normal : TAM_Insert;

    if (filerow < f->rows.size()) {
      const abuf &row = f->rows[filerow];
      const Size<Codepoint> NCOLS = 
        clampu<Size<Codepoint>>(row.ncodepoints(), g_editor.screencols - LINE_NUMBER_NUM_CHARS - 1);
      assert(g_editor.vim_mode == VM_NORMAL || g_editor.vim_mode == VM_INSERT);

      if (filerow == f->cursor.row) {
        for(Size<Codepoint> i(0); i <= NCOLS; ++i) {
          appendColWithCursor(&ab, &row, i, f->cursor.col, textAreaMode);
        }
      } else {
        for(Ix<Codepoint> i(0); i < NCOLS; ++i) {
          ab.appendCodepoint(row.getCodepoint(i));
        }
      }
    } else if (filerow == f->rows.size() && f->cursor.row == filerow) {
        abufAppendCodepointWithCursor(&ab, textAreaMode, " ");
    } 
    else {
        ab.appendstr("~");
    }

     if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr("\x1b[0m"); }

    // The K command (Erase In Line) erases part of the current line.
    // by default, arg is 0, which erases everything to the right of the
    // cursor.
    ab.appendstr("\x1b[K");

    // always append a space, since we decrement a row from screen rows
    // to make space for status bar.
    ab.appendstr("\r\n");
  }
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}

void drawCallback(std::function<void(abuf &ab)> f) {
  abuf ab;
  ab.appendstr("\x1b[?25l"); // hide cursor
  ab.appendstr("\x1b[2J");   // J: erase in display.
  ab.appendstr("\x1b[1;1H");  // H: cursor position
  f(ab);
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}


void editorDrawFileConfigPopup(FileConfig *f) {
  if (!f->progressbar.finished) {
    // draw progress info;
    drawCallback([f](abuf &ab) {
      const int CENTER_ROW = g_editor.screenrows;
      const int POPUP_SIZE = g_editor.screencols - 10;;
      const int POPUP_SIZE_L = POPUP_SIZE / 2;
      const int POPUP_SIZE_R = POPUP_SIZE - POPUP_SIZE_L;

      // ab.appendfmtstr(120, "\x1b[%d;%d", );
      ab.appendstr("┏");
      ab.appendstr("┃");
      ab.appendstr("┓");
      ab.appendstr("┗");
      ab.appendstr("┗");
      ab.appendstr("┛");        
    });
  }
};

void editorDrawInfoViewGoal(abuf *ab, const char *str) {
  const int NINDENT = 2;
  char INDENT_STR[NINDENT + 1];
  for(int i = 0; i < NINDENT; ++i) {
    INDENT_STR[i] = ' ';
  }
  INDENT_STR[NINDENT] = '\0';

  // this draws the "goal" object, which is a \n separated list of 'term : type' followed by a final
  // turnstile goal.
  int line_end = 0;
  const int len = strlen(str);

  while(line_end < len) {
    const int line_begin = line_end;
    for(; line_end < len && str[line_end] != '\n'; line_end++) {};
    assert(line_end <= len);
    if (line_end == len) {
      // we have reached end of string, so we are showing goal. Just splat directly.
      // sscanf
      // TODO: grab the turnstile out and color it.
      ab->appendfmtstr(120, "%s\x1b[0;36m%s\x1b[0m\x1b[K \r\n",
        INDENT_STR, str + line_begin);
    } else {
      // grab the substring and print out out.
      assert(line_end < len && str[line_end] == '\n');
      const int substr_len = line_end - line_begin; // half open interval [line_begin, line_end)     
      char *substr = strndup(str + line_begin, substr_len);
      ab->appendfmtstr(120, "%s%s\x1b[K \r\n", INDENT_STR, substr);
      free(substr);
    }
    line_end += 1; // skip over the newline
    assert(line_end >= line_begin);
  }

}

void editorDrawInfoViewTacticsTabbar(InfoViewTab tab, abuf *buf) {
    assert(tab >= 0 && tab < IVT_NumTabs);
    const char *str[IVT_NumTabs] = {"Tactics", "Hover","Messages"};    

    for(int i = 0; i < IVT_NumTabs; ++i) {
      if (i == (int) tab) {
        buf->appendstr("\x1b[1;97m");
      } else {
        buf->appendstr(ESCAPE_CODE_DULL);
      }
      buf->appendstr("┎");
      if (i == (int) tab) {
        buf->appendstr("■ ");
      } else {
        buf->appendstr("□ ");
      }
      
      buf->appendstr(str[i]);
      buf->appendstr("┓");

      buf->appendstr("\x1b[0m");

    }
    buf->appendstr("\r\n");
}

void editorDrawInfoViewTacticsTab(FileConfig *f) {
  abuf ab;

  ab.appendstr("\x1b[?25l"); // hide cursor
  // VT100 escapes.
  // \x1b: escape; J: erase in display.;[2J: clear entire screen
  // trivia: [0J: clear screen from top to cuursor, [1J: clear screen from
  // cursor to bottom
  //          0 is default arg, so [J: clear screen from cursor to bottom
  ab.appendstr("\x1b[2J");

  // H: cursor position
  // [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab.appendstr("\x1b[1;1H");

  // always append a space, since we decrement a row from screen rows
  // to make space for status bar.
  editorDrawInfoViewTacticsTabbar(IVT_Tactic, &ab);

  do {
    json_object  *result = nullptr;
    if (f->leanInfoViewPlainGoal.response) {
      json_object_object_get_ex(f->leanInfoViewPlainTermGoal.response, "result", &result);
    }
    if (result == nullptr) {
      ab.appendstr("▶ Expected Type: --- \x1b[K \r\n");
    } else {
      assert(json_object_get_type(result) == json_type_object);
      json_object *result_goal = nullptr;
      json_object_object_get_ex(result, "goal", &result_goal);
      assert(result_goal != nullptr);
      assert(json_object_get_type(result_goal) == json_type_string);

      ab.appendstr("▼ Expected Type: \x1b[K \r\n");
      editorDrawInfoViewGoal(&ab, json_object_get_string(result_goal));

    }
  } while(0); // end info view plain term goal.

  do {
    json_object  *result = nullptr;
    if (f->leanInfoViewPlainTermGoal.response) {
      json_object_object_get_ex(f->leanInfoViewPlainTermGoal.response, "result", &result);
    }
    if (result == nullptr) {
      ab.appendstr("▶ Tactic State: --- \x1b[K \r\n");
    } else {
      json_object *result_goals = nullptr;
      json_object_object_get_ex(result, "goals", &result_goals);
      assert(result_goals != nullptr);

      assert(json_object_get_type(result_goals) == json_type_array);
      if (json_object_array_length(result_goals) == 0) {
        ab.appendstr("▼ Tactic State: In tactic mode with no open tactic goal. \x1b[K \r\n");
        break;
      }
      assert(json_object_array_length(result_goals) > 0); 

      for(int i = 0; i < json_object_array_length(result_goals); ++i) {
        ab.appendfmtstr(120, "## goal[%d]: \x1b[K \r\n", i);
        json_object *result_goals_i = json_object_array_get_idx(result_goals, i);
        assert(result_goals_i != nullptr);
        assert(json_object_get_type(result_goals_i) == json_type_string);
        editorDrawInfoViewGoal(&ab, json_object_get_string(result_goals_i));
      }
    }
  } while(0); // end info view goal.

  // The K command (Erase In Line) erases part of the current line.
  // by default, arg is 0, which erases everything to the right of the
  // cursor.
  ab.appendstr("\x1b[K");

  // always append a space, since we decrement a row from screen rows
  // to make space for status bar.
  ab.appendstr("\r\n");

  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}

void editorDrawInfoViewMessagesTab(FileConfig *f) {
  abuf ab;
  ab.appendstr("\x1b[?25l"); // hide cursor
  ab.appendstr("\x1b[2J");   // J: erase in display.
  ab.appendstr("\x1b[1;1H");  // H: cursor position
  editorDrawInfoViewTacticsTabbar(IVT_Messages, &ab);
  ab.appendstr("\x1b[K"); // The K command (Erase In Line) erases part of the current line.
  ab.appendstr("\r\n");  // always append a space

  const int MAXROWS = 20;
  for(int i = 0; i < f->lspDiagnostics.size() && i < MAXROWS; ++i) {
    const LspDiagnostic d = f->lspDiagnostics[i];
    const int MAXCOLS = 100;
    assert(d.version == f->text_document_item.version);
    if (d.severity == LspDiagnosticSeverity::Error) {
      ab.appendstr("ERR : ");
    } else if (d.severity == LspDiagnosticSeverity::Warning) {
      ab.appendstr("WARN: ");
    } else if (d.severity == LspDiagnosticSeverity::Information) {
      ab.appendstr("INFO: ");
    } else {
      assert(d.severity == LspDiagnosticSeverity::Hint);
      ab.appendstr("HINT: ");
    }
    ab.appendfmtstr(120, "%d:%d: ", d.range.start.row, d.range.start.col);
    std::string message_sub = d.message.substr(0, MAXCOLS - 10);
    ab.appendstr(message_sub.c_str());
    ab.appendstr("\r\n");
  }

  ab.appendstr("\x1b[K"); // The K command (Erase In Line) erases part of the current line.
  ab.appendstr("\r\n");  // always append a space
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));

}


void editorDrawInfoViewHoverTab(FileConfig *f) {
  abuf ab;
  ab.appendstr("\x1b[?25l"); // hide cursor
  ab.appendstr("\x1b[2J");   // J: erase in display.
  ab.appendstr("\x1b[1;1H");  // H: cursor position
  editorDrawInfoViewTacticsTabbar(IVT_Hover, &ab);
  ab.appendstr("\x1b[K"); // The K command (Erase In Line) erases part of the current line.
  ab.appendstr("\r\n");  // always append a space

  do {
    json_object  *result = nullptr;
    if (f->leanHoverViewHover.response) {
      json_object_object_get_ex(f->leanHoverViewHover.response, "result", &result);
    }
    if (result == nullptr) {
      ab.appendstr("▶ Hover: --- \x1b[K \r\n");
    } else {
      json_object *result_contents = nullptr;
      json_object_object_get_ex(result, "contents", &result_contents);
      assert(result_contents != nullptr);

      json_object *result_contents_value = nullptr;
      json_object_object_get_ex(result_contents, "value", &result_contents_value);
      assert (result_contents_value != nullptr);
      assert(json_object_get_type(result_contents_value) == json_type_string);
      const char *s = json_object_get_string(result_contents_value); 
      ab.appendstr("▼ Hover: \x1b[K \r\n");
      std::vector<std::string> lines; lines.push_back(std::string());
      for(int i = 0; i < strlen(s); ++i) {
        std::string &line = lines[lines.size() - 1];
        if (s[i] == '\n') {
          lines.push_back(std::string());
        } else if (line.size() > 100) {
          line += "–"; // line break
          lines.push_back(std::string());
        } else {
          line += s[i];
        }
      } // end while.

      for(std::string &line : lines) {
        ab.appendstr(line.c_str());
        ab.appendstr("\x1b[K \r\n");
      }
    } // end else 'result != nullptr.
  } while(0);

  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));

}

InfoViewTab _infoViewTabCycleDelta(FileConfig *f, InfoViewTab t, int delta) {
  return InfoViewTab(((int) t + delta) % IVT_NumTabs);
}

InfoViewTab infoViewTabCycleNext(FileConfig *f, InfoViewTab t) {
  return _infoViewTabCycleDelta(f, t, +1);
}

InfoViewTab infoViewTabCyclePrevious(FileConfig *f, InfoViewTab t) {
  return _infoViewTabCycleDelta(f, t, +IVT_NumTabs-1);
}

void editorDrawInfoView(FileConfig *f) {
  if(f->infoViewTab == IVT_Tactic) {
      editorDrawInfoViewTacticsTab(f);
  } else if(f->infoViewTab == IVT_Hover) {
      editorDrawInfoViewHoverTab(f);
  } else if (f->infoViewTab == IVT_Messages) {
    editorDrawInfoViewMessagesTab(f);
  }

  assert(false && "unreachable, should not reach here in drawInfoView.");
}

void editorDrawCompletionMode() {
  abuf ab;
  // VT100 escapes.
  // \x1b: escape. J: erase in display. [2J: clear entire screen
  ab.appendstr("\x1b[2J");
  // H: cursor position. [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab.appendstr("\x1b[1;1H");
  
  ab.appendstr("@@@ COMPLETION MODE \r\n");
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}

void editorDrawNoFile() {
  abuf ab;
  ab.appendstr("\x1b[?25l"); // hide cursor
  // VT100 escapes.
  // \x1b: escape.
  // J: erase in display.
  // [2J: clear entire screen
  // trivia: [0J: clear screen from top to cuursor, [1J: clear screen from
  // cursor to bottom
  //          0 is default arg, so [J: clear screen from cursor to bottom
  ab.appendstr("\x1b[2J");
  // H: cursor position
  // [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab.appendstr("\x1b[1;1H");
  ab.appendstr("We will know . We must know ~ David Hilbert.\r\n");
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}

void editorDraw() {
  if (g_editor.vim_mode == VM_NORMAL || g_editor.vim_mode == VM_INSERT) {
    if (g_editor.curFile()) {
      fileConfigDraw(g_editor.curFile());
    } else {
      editorDrawNoFile();
    }
    // editorDrawFileConfigPopup(); // draw file config mode popup, if it needs to be drawn.
  } else if (g_editor.vim_mode == VM_COMPLETION) {
    editorDrawCompletionMode();
  } else if (g_editor.vim_mode == VM_CTRLP) {
    ctrlpDraw(&g_editor.ctrlp);
  } else if (g_editor.vim_mode == VM_TILDE) {
    tilde::tildeDraw(&tilde::g_tilde);
  }else {
    assert(g_editor.vim_mode == VM_INFOVIEW_DISPLAY_GOAL);
    assert(g_editor.curFile() != NULL);
    editorDrawInfoView(g_editor.curFile());
  }
}

/*** input ***/

// row can be `NULL` if one wants to indicate the last, sentinel row.
/*
RowMotionRequest fileRowMoveCursorIntraRow(Cursor *cursor, abuf *row, LRDirection dir) {
  if (dir == LRDirection::None) { return RowMotionRequest::None; }

  Cursor cursorNew = cursor;
  cursorNew.col = col + Size<Codepoint>(dir);

  if (cursorNew.col < 0) {
    return RowMotionRequest::Up; // move up.
  } else if (cursorNew.col > row->ncodepoints()) {
    if (row) {
      return RowMotionRequest::Down; // move down, since we have a row to move down to.
    } else {
      return RowMotionRequest::None; // no motion necessary.
    }
  } else {
    *cursor = cursorNew // cursor is inbounds.
    return RowMotionRequest::None;    
  }
}
*/


enum CharacterType {
  Sigil, // (, <, etc.
  Alnum, // a-z, \alpha, \beta, etc.
  Space // ' ', '\t', etc.
};

CharacterType getCodepointType(const char *codepoint) {
  char c = *codepoint;
  if (c == ' ' || c == '\t') { return CharacterType::Space; }
  if ((c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return Alnum;
  } else {
    return Sigil;
  }
}

void fileConfigCursorMoveWordNext(FileConfig *f) {
  assert (f->cursor.row >= 0);
  assert (f->cursor.row <= f->rows.size());
  // stop at end of line.
  if (f->cursor.row == f->rows.size()) { return; }
  assert (f->cursor.row < f->rows.size());
  abuf *row = &f->rows[f->cursor.row];

  if (f->cursor.col == row->ncodepoints()) {
    f->cursor.row++;
    f->cursor.col = Size<Codepoint>(0);
    return;
  }
  assert(f->cursor.col < row->ncodepoints());  
  f->cursor.col = f->cursor.col.next(); // advance.

  assert(f->cursor.col > Size<Codepoint>(0));
  for(; f->cursor.col < row->ncodepoints(); f->cursor.col = f->cursor.col.next()) {
    CharacterType ccur = getCodepointType(row->getCodepoint(f->cursor.col.toIx()));
    CharacterType cprev = getCodepointType(row->getCodepoint(f->cursor.col.prev().toIx()));
    if (ccur == CharacterType::Space) {
      continue;
    }
    if (ccur == CharacterType::Sigil) {
      // we are at a sigil, stop.
      return;
    }
    else if (cprev != ccur) { 
      // we changed the kind of character, stop.
      return; 
    }
  }   

};

void fileConfigCursorMoveWordPrev(FileConfig *f) {
  assert (f->cursor.row >= 0);
  assert (f->cursor.row <= f->rows.size());

  if (f->cursor.row == f->rows.size()) {
    assert(f->cursor.col == Size<Codepoint>(0));
  }

  if (f->cursor.col == Size<Codepoint>(0)) {
    if (f->cursor.row == 0) { return; }
    f->cursor.row--;
    f->cursor.col = f->rows[f->cursor.row].ncodepoints();
    return;
  }
  assert(f->cursor.row < f->rows.size());
  abuf *row = &f->rows[f->cursor.row];

  assert(f->cursor.col > Size<Codepoint>(0));
  f->cursor.col = f->cursor.col.prev(); // advance.



  for(; f->cursor.col > Size<Codepoint>(0); f->cursor.col = f->cursor.col.prev()) {
    assert(f->cursor.col < row->ncodepoints());

    CharacterType ccur = getCodepointType(row->getCodepoint(f->cursor.col.toIx()));
    CharacterType cprev = getCodepointType(row->getCodepoint(f->cursor.col.prev().toIx()));
    if (ccur == CharacterType::Space) {
      continue;
    }
    if (ccur == CharacterType::Sigil) {
      // we are at a sigil, stop.
      return;
    }
    else if (cprev != ccur) { 
      // we changed the kind of character, stop.
      return; 
    }
  }   

};


void fileConfigCursorMoveCharLeft(FileConfig *f) {
  if (f->cursor.col > Size<Codepoint>(0)) {
    f->cursor.col--;
  } else if (f->cursor.row > 0) {
    assert(f->cursor.col == Size<Codepoint>(0));
    f->cursor.row--;
    f->cursor.col = f->rows[f->cursor.row].ncodepoints();
  }
}

void fileConfigCursorMoveCharRight(FileConfig *f) {
  if (f->cursor.row < f->rows.size()) {
    if (f->cursor.col < f->rows[f->cursor.row].ncodepoints()) {
      f->cursor.col++;
    } else {
      assert(f->cursor.col == f->rows[f->cursor.row].ncodepoints());
      f->cursor.row++;
      f->cursor.col = Size<Codepoint>(0);
    }
  }
}

void fileConfigCursorMoveEndOfRow(FileConfig *f) {
  if (f->cursor.row < f->rows.size()) {
    f->cursor.col = f->rows[f->cursor.row].ncodepoints();
  }
}

void fileConfigCursorMoveBeginOfRow(FileConfig *f) {
  if (f->cursor.row < f->rows.size()) {
    f->cursor.col = Size<Codepoint>(0);
  }
}

// move cursor to the right, and do not wraparound to next row. 
void fileConfigCursorMoveCharRightNoWraparound(FileConfig *f) {
  if (f->cursor.row < f->rows.size()) {
    if (f->cursor.col < f->rows[f->cursor.row].ncodepoints()) {
      f->cursor.col++;
    } 
  }
}

// delete till the end of the row.
void fileConfigDeleteTillEndOfRow(FileConfig *f) {
  if (f->cursor.row < f->rows.size()) {
    abuf *row = &f->rows[f->cursor.row]; 
    while(row->ncodepoints() > f->cursor.col) {
      row->delCodepointAt(row->ncodepoints().largestIx());
    }
    f->makeDirty();
  }
}


void fileConfigMoveCursor(FileConfig *f, int key) {
  switch (key) {
  case 'w': {
    fileConfigCursorMoveWordNext(f);
    break;
  }
  case 'b': {
    fileConfigCursorMoveWordPrev(f);
    break;
  }

  case 'h': {
    fileConfigCursorMoveCharLeft(f);
    break;
  }
  case 'l':
    fileConfigCursorMoveCharRight(f);
    break;
  case 'k':
    if (f->cursor.row > 0) {
      f->cursor.row--;
    }
    f->cursor.col = 
      std::min<Size<Codepoint>>(f->cursor.col, f->rows[f->cursor.row].ncodepoints());
    break;
  case 'j':
    if (f->cursor.row < f->rows.size()) {
      f->cursor.row++;
    }
    if (f->cursor.row < f->rows.size()) {
      f->cursor.col = 
        std::min<Size<Codepoint>>(f->cursor.col, f->rows[f->cursor.row].ncodepoints());
    }
    break;
  case CTRL_KEY('d'):
    f->cursor.row = clampu<int>(f->cursor.row + g_editor.screenrows / 4, f->rows.size());
    if (f->cursor.row < f->rows.size()) {
      f->cursor.col = 
        std::min<Size<Codepoint>>(f->cursor.col, f->rows[f->cursor.row].ncodepoints());
    }
    break;
  case CTRL_KEY('u'):
    f->cursor.row = clamp0<int>(f->cursor.row - g_editor.screenrows / 4);
    f->cursor.col = 
      std::min<Size<Codepoint>>(f->cursor.col, f->rows[f->cursor.row].ncodepoints());
    break;
  }
}


int editorReadRawEscapeSequence() {
  int nread;
  char c;
  if ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)  {
      die("unable to read");
    }
    return 0; // no input. is it safe to return 0?
  }

  // if we see an escape char, then read.
  if (c == '\x1b') {
    char seq[3];

    // user pressed escape key only!
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    // user pressed escape sequence!
    // ... Oh wow, that's why it's called an ESCAPE sequence.
    if (seq[0] == '[') {

      // [<digit>
      if (seq[1] >= '0' && seq[1] <= '9') {
        // read digit.
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        // [<digit>~
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '5':
            return KEYEVENT_PAGE_UP;
          case '6':
            return KEYEVENT_PAGE_UP;
          }
        }
      }
      // translate arrow keys to vim :)
      switch (seq[1]) {
      case 'A':
        return KEYEVENT_ARROW_UP;
      case 'B':
        return KEYEVENT_ARROW_UP;
      case 'C':
        return KEYEVENT_ARROW_UP;
      case 'D':
        return KEYEVENT_ARROW_UP;
      }
    };
    return '\x1b';
  }
  else if (c == 127) {
    return KEYEVENT_BACKSPACE;
  }
  return c;
}


// open row below ('o'.)
void fileConfigOpenRowBelow(FileConfig *f) {
  if (f->cursor.row == f->rows.size()) {
    fileConfigInsertRowBefore(f, f->cursor.row, nullptr, 0);
  } else {
    fileConfigInsertRowBefore(f, f->cursor.row + 1, nullptr, 0);
    f->cursor.row += 1;
    f->cursor.col = Size<Codepoint>(0);
  }
};

// open row above ('O');
void fileConfigOpenRowAbove(FileConfig *f) {
  fileConfigInsertRowBefore(f, 0, nullptr, 0);
}

void editorHandleGotoResponse(json_object_ptr response) {
   tilde::tildeWrite("Response [textDocument/definition] %s", json_object_to_json_string(response));
   // Response [textDocument/definition] { "result": [ { "targetUri": 
   //     "file://path/to/file.lean", 
   //     "targetSelectionRange": { "start": { "line": 39, "character": 10 }, "end": { "line": 39, "character": 13 } },
   //     "targetRange": { "start": { "line": 39, "character": 0 }, "end": { "line": 44, "character": 23 } },
   //     "originSelectionRange": { "start": { "line": 3, "character": 6 }, "end": { "line": 3, "character": 9 } } } ],
   // "jsonrpc": "2.0", "id": 10 }  
   // targetSelectionRange: range to be highlighted within editor.
   // targetRange: full range of defn.

    json_object *result = nullptr;
    json_object_object_get_ex(response, "result", &result);
    if (result == nullptr) { return ; }; 
    assert(json_object_get_type(result) == json_type_array);
    assert(result);
    tilde::tildeWrite("response [textDocument/gotoDefinition] result: '%s'", 
      json_object_to_json_string(result));

    assert(json_object_get_type(result) == json_type_array);
    if (json_object_array_length(result) == 0) {
      tilde::tildeWrite("response [textDocument/gotoDefinition]: length 0 :(");
      return;
    }

    json_object *result0 = json_object_array_get_idx(result, 0);
    if (result == nullptr) { return; }
    assert(json_object_get_type(result0) == json_type_object);
    assert(result0);

    tilde::tildeWrite("response [textDocument/gotoDefinition] result0: '%s'", 
      json_object_to_json_string(result0));

    json_object *result0_uri = nullptr;
    json_object_object_get_ex(result0, "targetUri", &result0_uri);
    tilde::tildeWrite("response [textDocument/gotoDefinition] result0_uri[raw]: '%s'", 
      json_object_to_json_string(result0_uri));
    assert(result0_uri);
    assert(json_object_get_type(result0_uri) == json_type_string);
    const char *uri = json_object_get_string(result0_uri);
    assert(result0_uri);
    const fs::path absolute_filepath = Uri::parse(uri);
    tilde::tildeWrite("response [textDocument/gotoDefinition] result0_uri[filepath]: '%s'", std::string(absolute_filepath).c_str());
    assert(absolute_filepath.is_absolute()); 


    json_object *result0_targetSelectionRange = nullptr;
    json_object_object_get_ex(result0, "targetSelectionRange", &result0_targetSelectionRange);

    json_object *result0_targetSelectionRange_start = nullptr;
    json_object_object_get_ex(result0_targetSelectionRange, "start", &result0_targetSelectionRange_start);
    assert(result0_targetSelectionRange_start);
    tilde::tildeWrite("response [textDocument/gotoDefinition] result.0.targetSelectionRange.start: '%s'", 
        json_object_to_json_string(result0_targetSelectionRange_start));

    LspPosition p = json_object_parse_position(result0_targetSelectionRange_start);
    tilde::tildeWrite("response [textDocument/gotoDefinition] position: (%d, %d)", p.row, p.col);

    FileLocation loc(absolute_filepath, LspPositionToCursor(p));
    tilde::tildeWrite("goto definition file location cursor (%d, %d)", 
        loc.cursor.row, loc.cursor.col.size);
    g_editor.getOrOpenNewFile(loc);
}

void editorTickPostKeypress() {
  FileConfig *f = g_editor.curFile();
  if (!f) { return; }


  tilde::tildeWrite("editorTick() | nresps: %d | nunhandled: %d",
    f->lean_server_state.request2response.size(), f->lean_server_state.unhandled_server_requests.size());

  if (f->absolute_filepath.extension() != ".lean") {
    return;
  }
  assert(f->absolute_filepath.extension() == ".lean");
  if (!f->lean_server_state.initialized) {
    fileConfigLaunchLeanServer(f);
  }
  assert(f->lean_server_state.initialized);
  fileConfigSyncLeanState(f);
  assert (f->text_document_item.is_initialized);

  f->lean_server_state.tick_nonblocking();

  if (lspServerFillJsonNonblockingResponse(f->lean_server_state, f->leanGotoRequest)) {
    editorHandleGotoResponse(f->leanGotoRequest.response);
  }
  lspServerFillJsonNonblockingResponse(f->lean_server_state, f->leanInfoViewPlainGoal);
  lspServerFillJsonNonblockingResponse(f->lean_server_state, f->leanInfoViewPlainTermGoal);
  lspServerFillJsonNonblockingResponse(f->lean_server_state, f->leanHoverViewHover);

  // handle unhandled requests
  if (f->lean_server_state.unhandled_server_requests.size() == 0) {
    return;
  }

  json_object_ptr req = f->lean_server_state.unhandled_server_requests.front();
  f->lean_server_state.unhandled_server_requests.erase(f->lean_server_state.unhandled_server_requests.begin());

  tilde::tildeWrite("%s: unhandled request is: %s", __PRETTY_FUNCTION__, json_object_to_json_string(req));
  
  json_object *methodo = NULL;
  json_object_object_get_ex(req, "method", &methodo);
  assert(methodo);
  const char *method = json_object_get_string(methodo);
  assert(method);


  if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
    json_object *paramso =  NULL;
    json_object_object_get_ex(req, "params", &paramso);
    assert(paramso);

    json_object *versiono = NULL;
    json_object_object_get_ex(paramso, "version", &versiono);      
    assert(versiono);
    const int version = json_object_get_int(versiono);

    assert (version <= f->text_document_item.version);
    if (version < f->text_document_item.version) {
      return; // skip, since it's older than what we want.
    }
    assert(version == f->text_document_item.version);

    
    json_object *ds = NULL;
    json_object_object_get_ex(paramso, "diagnostics", &ds);
    assert(ds);
    const int NDS = json_object_array_length(ds);

    // https://github.com/microsoft/language-server-protocol/issues/228
    // diagnostics are cleared when the server sends [], and are otherwise union'd.
    f->lspDiagnostics.clear();

    for(int i = 0; i < NDS; ++i) {
      json_object *di = json_object_array_get_idx(ds, i);
      LspDiagnostic d = json_parse_lsp_diagnostic(di, version);
      f->lspDiagnostics.push_back(d);
    }
    tilde::tildeWrite("  textDocument/publishDiagnostics #messages: '%d'", f->lspDiagnostics.size());
  } else if (strcmp(method, "$/lean/fileProgress") == 0) {
    json_object *paramso =  NULL;
    json_object_object_get_ex(req, "params", &paramso);
    assert(paramso);

    json_object *processingo = NULL;
    json_object_object_get_ex(paramso, "processing", &processingo);      
    assert(processingo);
    const int nrecords = json_object_array_length(processingo);

    assert(nrecords <= 1 && "file progress should have at most 1 record");
    if (nrecords == 0) {
      f->progressbar.finished = true;
      return;
    }

    assert(nrecords == 1);
    f->progressbar.finished = false;

    json_object *processingo_recordo = json_object_array_get_idx(processingo, 0);
    assert(processingo_recordo);

    json_object *rangeo = NULL;
    json_object_object_get_ex(processingo_recordo, "range", &rangeo);
    assert(rangeo);
    LspRange range = json_object_parse_range(rangeo);
    f->progressbar.startRow = range.start.row;
    f->progressbar.endRow = range.end.row;
  }
};

void editorProcessKeypress() {
  int c = editorReadRawEscapeSequence();

  if (g_editor.vim_mode == VM_COMPLETION) {
    if (c == CTRL_KEY('\\') ||  c == CTRL_KEY('c')) {
      g_editor.vim_mode = VM_INSERT; return;
    }
    else if (c == '\r') {
      // TODO: make the completer accept the autocomplete.
      return;
    }
    else if (isprint(c)){
      // completionInsertChar(c);
      return;
    }
  }
  else if (g_editor.vim_mode == VM_TILDE) {
      tilde::tildeHandleInput(&tilde::g_tilde, c);
      if (tilde::tildeWhenQuit(&tilde::g_tilde)) {
        g_editor.vim_mode = VM_NORMAL;
        return;
      }
  }
  else if (g_editor.vim_mode == VM_CTRLP) {
    // if lakefile is available, use it as the path.
    // if not, use the file path as the base path.
    // TODO: search for `.git` and use it as the base path.
    // TODO: |g_editor basePath, refactor into separate file.|
    // char *default_basepath = strdup(
    //   f->lean_server_state.lakefile_dirpath ?
    //   f->lean_server_state.lakefile_dirpath :
    //   dirname(f->absolute_filepath));
    ctrlpHandleInput(&g_editor.ctrlp, c);
    // free(default_basepath);
    if (ctrlpWhenQuit(&g_editor.ctrlp)) {
      g_editor.vim_mode = g_editor.ctrlp.previous_state;
      return;
    }

    if (ctrlpWhenSelected(&g_editor.ctrlp)) {
      g_editor.getOrOpenNewFile(ctrlpGetSelectedFileLocation(&g_editor.ctrlp));
      g_editor.vim_mode = g_editor.ctrlp.previous_state;
      return;
    }

  }
  else if (g_editor.vim_mode == VM_INFOVIEW_DISPLAY_GOAL) { // behaviours only in infoview mode
    assert(g_editor.curFile());
    FileConfig *f = g_editor.curFile();
    switch(c) {
    case 'h':
    case 'j': {
      f->infoViewTab = infoViewTabCyclePrevious(f, f->infoViewTab);
      return;
    }
    case 'l':
    case 'k': 
    case '\t': {
      // TODO: this is not per file info? or is it?
      f->infoViewTab = infoViewTabCycleNext(f, f->infoViewTab);
      return;
    }
    case CTRL_KEY('c'):
    case 'q':
    case 'g':
    case ' ':
    case '\r':
    case '?': {
      g_editor.vim_mode = VM_NORMAL;
      return;
    }
    default: {
      return;
    }
    }
  }
  else if (g_editor.vim_mode == VM_NORMAL) { // behaviours only in normal mode
    FileConfig *f = g_editor.curFile();
    if (f == nullptr) {
      if (c ==  CTRL_KEY('p')) {
        ctrlpOpen(&g_editor.ctrlp, VM_NORMAL, g_editor.original_cwd);
      } else if (c == 'q') {
        exit(0);
      } else if (c == '`') {
        tilde::tildeOpen(&tilde::g_tilde);
        g_editor.vim_mode = VM_TILDE;
        return;
      }
      return;
    }

    assert(f != nullptr);
    switch (c) {
    case CTRL_KEY(']'): {
      // goto definition.
      // TODO: refactor this code. to force initialization first.
      if (!f->lean_server_state.initialized) {
        fileConfigLaunchLeanServer(f);
      }
      assert(f->lean_server_state.initialized);
      fileConfigSyncLeanState(f);
      fileConfigGotoDefinitionNonblocking(g_editor.curFile(), GotoKind::Definition);
      return;
    }
    case CTRL_KEY('['): { // is this a good choice of key? I am genuinely unsure.
      // goto definition.
      // TODO: refactor this code. to force initialization first.
      if (!f->lean_server_state.initialized) {
        fileConfigLaunchLeanServer(f);
      }
      assert(f->lean_server_state.initialized);
      fileConfigSyncLeanState(f);
      fileConfigGotoDefinitionNonblocking(g_editor.curFile(), GotoKind::TypeDefiition);
      return;
    }
    case CTRL_KEY('o'): {
      g_editor.undoFileMove();
      return;
    }
    case CTRL_KEY('i'): {
      g_editor.redoFileMove();
      return;
    }
    case '`': {
      tilde::tildeOpen(&tilde::g_tilde);
      g_editor.vim_mode = VM_TILDE;
      return;
    }
    case CTRL_KEY('p'): {
      ctrlpOpen(&g_editor.ctrlp, VM_NORMAL, g_editor.original_cwd);
      return;
    }
    case 'D': {
      f->mkUndoMemento();
      fileConfigDeleteTillEndOfRow(f);
      return;
    }
    case 'q': {
      fileConfigSave(f);
      exit(0);
      return;
    }
    case 'u': {
      f->doUndo();
      return;
    }
    case 'r':
    case 'U': {
      f->doRedo();
      return;

    }
    case 'x': {
      f->mkUndoMemento();
      fileConfigXCommand(f);
      break;
    }
    case 'h':
    case 'j':
    case 'k':
    case 'l':
    case CTRL_KEY('d'):
    case CTRL_KEY('u'):
    case 'w': 
    case 'b': {
      fileConfigMoveCursor(f, c);
      break;
    }
    case 'o': {
      f->mkUndoMemento();
      fileConfigOpenRowBelow(f);
      g_editor.vim_mode = VM_INSERT;
      return;
    }
    case 'O': {
      f->mkUndoMemento();
      fileConfigOpenRowAbove(f);
      g_editor.vim_mode = VM_INSERT;
      return; 
    }

    case 'a': {
      f->mkUndoMemento();
      fileConfigCursorMoveCharRightNoWraparound(f);
      g_editor.vim_mode = VM_INSERT;
      return;
    }
    case 'd': {
      f->mkUndoMemento();
      fileConfigDeleteCurrentRow(f);
      return;
    }
    case '$': {
      fileConfigCursorMoveEndOfRow(f);
      return;
    }
    case '0': {
      fileConfigCursorMoveBeginOfRow(f);
      return;
    }

    case 'g':
    case ' ':
    case '\r':
    case '?': {
      // TODO: dear god get right of this code duplication.
      if (f->absolute_filepath.extension() == ".lean") {
        if (!f->lean_server_state.initialized) {
          // TODO: switch to `std::optional`
          fileConfigLaunchLeanServer(f);
        }
        assert(f->lean_server_state.initialized);
        fileConfigSyncLeanState(f);
        // TODO: make this more local.
        fileConfigRequestGoalState(f);
        g_editor.vim_mode = VM_INFOVIEW_DISPLAY_GOAL;
      }
      return;

    }
    case 'i':
      f->mkUndoMemento();
      g_editor.vim_mode = VM_INSERT;
      return;
    } // end switch over key.
  } // end mode == VM_NORMAL
  else {
    assert (g_editor.vim_mode == VM_INSERT); 
    FileConfig *f = g_editor.curFile();
    if (f == nullptr) {
      if (c ==  CTRL_KEY('p')) {
        ctrlpOpen(&g_editor.ctrlp, VM_INSERT, g_editor.original_cwd);
      } else if (c == 'q') {
        exit(0);
      }
      return;
    }
    assert(f != nullptr);
    // make an undo memento every second.
    f->mkUndoMementoRecent();

    switch (c) { // behaviors only in edit mode.
    // case CTRL_KEY('\\'): {
    //   ctrlpOpen(&g_editor.ctrlp, VM_COMPLETION, g_editor.original_cwd);
    //   return;
    // }
    case CTRL_KEY('p'): {
      ctrlpOpen(&g_editor.ctrlp, VM_INSERT, g_editor.original_cwd);
      return;
    }
    case '\r':
      fileConfigInsertEnterKey(f);
      return;
    case KEYEVENT_BACKSPACE: { // this is backspace, apparently
      fileConfigBackspace(f);
      return;
    }
    // when switching to normal mode, sync the lean state. 
    case CTRL_KEY('c'): { // escape key
      g_editor.vim_mode = VM_NORMAL;
      fileConfigSave(f);
      return;
   }
    default:
      if (isprint(c)){
        fileConfigInsertCharBeforeCursor(f, c);
        return;
      }
    } // end switch case.
  } // end mode == VM_INSERT
}

/*** init ***/

void initEditor() {
  if (getWindowSize(&g_editor.screenrows, &g_editor.screencols) == -1) {
    die("getWindowSize");
  };
  static const int BOTTOM_INFO_PANE_HEIGHT = 2; g_editor.screenrows -= BOTTOM_INFO_PANE_HEIGHT;
  fs::path abbrev_path = get_abbreviations_dict_path();
  printf("[loading abbreviations.json from '%s']\n", abbrev_path.c_str());
  load_abbreviation_dict_from_file(&g_editor.abbrevDict, abbrev_path);
  assert(g_editor.abbrevDict.is_initialized);
}

// get length <len> such that
//    buf[:finalix) = "boo \alpha"
//    buf[:finalix - len] = "boo" and buf[finalix - len:finalix) = "\alpha".
// this is such that buf[finalix - len] = '\' if '\' exists,
//     and otherwise returns len = 0.
int suffix_get_unabbrev_len(const char *buf, int finalix, const char *unabbrev, int unabbrevlen) {
    int slashix = -1;
  for(int i = finalix; i >= 0; --i) {
    if (buf[i] == '\\') {
      slashix = i;
      break;
    }

    // length 0
    if (slashix  == finalix) {
      return 0;
    }

    // do not cross word boundaries, no match.
    if (buf[i] == ' ' || buf[i] == '\t') { return 0; }
  }
  // no slash found, no match.
  if (slashix == -1) { return 0; }
  assert(slashix >= 0 && buf[slashix] == '\\');
  // (slashix, finalix]
  const int needlelen = finalix - slashix;

  // we have e.g. '\toba' while abbreviation is 'to'. Do not accept this case.
  if (needlelen > unabbrevlen) { return 0; }
  else if (needlelen == unabbrevlen && strncmp(buf + slashix + 1, unabbrev, needlelen) == 0) { 
    // we have e.g. '\to' while abbreviation is '\to'. return a partial match.
    return needlelen;
  }
  else if (needlelen < unabbrevlen && strncmp(buf + slashix + 1, unabbrev, needlelen) == 0) {
    // we have e.g. '\t' while abbreviation is '\to'. check for a partial match.
    return needlelen;
  }
  return 0;
}

AbbrevMatchKind suffix_is_unabbrev(const char *buf, int finalix, const char *unabbrev, int unabbrevlen) {
  int slashix = -1;
  for(int i = finalix; i >= 0; --i) {
    if (buf[i] == '\\') {
      slashix = i;
      break;
    }

    // we disallow the empty match.
    if (slashix  == finalix) {
      return AbbrevMatchKind::AMK_EMPTY_STRING_MATCH;
    }

    // do not cross word boundaries/
    if (buf[i] == ' ' || buf[i] == '\t') { return AMK_NOMATCH; }
  }
  if (slashix == -1) { return AMK_NOMATCH; }
  assert(slashix >= 0 && buf[slashix] == '\\');
  // (slashix, finalix]
  const int needlelen = finalix - slashix;

  // we have e.g. '\toba' while abbreviation is 'to'. Do not accept this case.
  if (needlelen > unabbrevlen) { return AMK_NOMATCH; }
  else if (needlelen == unabbrevlen && strncmp(buf + slashix + 1, unabbrev, needlelen) == 0) { 
    // we have e.g. '\to' while abbreviation is '\to'. return a partial match.
    return AMK_EXACT_MATCH;
  }
  else if (needlelen < unabbrevlen && strncmp(buf + slashix + 1, unabbrev, needlelen) == 0) {
    // we have e.g. '\t' while abbreviation is '\to'. check for a partial match.
    return AMK_PREFIX_MATCH;
  }
  return AMK_NOMATCH;
}

const char *abbrev_match_kind_to_str(AbbrevMatchKind kind) {
  switch(kind) {
  case AMK_NOMATCH: return "nomatch";
  case AMK_PREFIX_MATCH: return "prefix_match";
  case AMK_EXACT_MATCH: return "exact_match";
  case AMK_EMPTY_STRING_MATCH: return "empty_string";
  }
  assert(false && "unable to convert abbrev match kind to string");
}


// return the index of the all matches, for whatever match exists. Sorted to be matches 
// where the match string has the smallest length to the largest length.
// This ensures that the AMK_EXACT_MATCHes will occur at the head of the list.
void abbrev_dict_get_matching_unabbrev_ixs(AbbreviationDict *dict, 
  const char *buf,
  int finalix,
  std::vector<int> *matchixs) {
  assert(dict->is_initialized);
  for(int i = 0; i < dict->nrecords; ++i) {
    if (suffix_is_unabbrev(buf, finalix, dict->unabbrevs[i], dict->unabbrevs_len[i])) {
      matchixs->push_back(i);
    }
  }
  std::sort(matchixs->begin(), matchixs->end(), [&](int i, int j) {
    return dict->unabbrevs_len[i] < dict->unabbrevs_len[j];
  });

}

// return the best unabbreviation for the suffix of `buf`.
SuffixUnabbrevInfo abbrev_dict_get_unabbrev(AbbreviationDict *dict, const char *buf, int finalix) {
  std::vector<int> matchixs;
  abbrev_dict_get_matching_unabbrev_ixs(dict,
    buf,
    finalix,
    &matchixs);

  if (matchixs.size() == 0) {
    return SuffixUnabbrevInfo::nomatch();
  } else{
    SuffixUnabbrevInfo out;
    out.matchix = matchixs[0];
    out.matchlen = suffix_get_unabbrev_len(buf, finalix, dict->unabbrevs[out.matchix], dict->unabbrevs_len[out.matchix]);
    out.kind = suffix_is_unabbrev(buf, finalix, dict->unabbrevs[out.matchix], dict->unabbrevs_len[out.matchix]);
    return out;
  }
};

// get the path to the executable, so we can build the path to resources.
fs::path get_executable_path() {
  const int BUFSIZE = 2048;
  char *buf = (char*)calloc(BUFSIZE, sizeof(char));
  const char *exe_path = "/proc/self/exe";
  int sz = readlink(exe_path, buf, BUFSIZE);

  if (sz == -1) {
    die("unable to get path of executable, failed to read '%s'.", exe_path);
  }
  fs::path out(buf);
  free(buf);
  return out;
}

// get the path to `/path/to/exe/abbreviations.json`.
fs::path get_abbreviations_dict_path() {
  fs::path exe_path = get_executable_path();
  fs::path exe_folder = exe_path.parent_path();
  // char *exe_folder = dirname(strdup(exe_path.c_str()));
  return exe_folder / "abbreviations.json";
}


// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_json(AbbreviationDict *dict, json_object *o) {
  assert(o && "illegal json file");
  assert(!dict->is_initialized);
  dict->nrecords = 0;

  json_object_object_foreach(o, key_, val_) {
    dict->nrecords++;
    (void)key_;
  }

  dict->unabbrevs = (char **)calloc(sizeof(char*), dict->nrecords);
  dict->abbrevs = (char **)calloc(sizeof(char*), dict->nrecords);
  dict->unabbrevs_len = (int *)calloc(sizeof(int), dict->nrecords);

  int i = 0;
  json_object_object_foreach(o, key0, val0) {
    assert(json_object_get_type(val0) == json_type_string);
    const char *val_str = json_object_get_string(val0);
    dict->unabbrevs[i] = strdup(key0);
    dict->abbrevs[i] = strdup(val_str);
    dict->unabbrevs_len[i] = strlen(dict->unabbrevs[i]);
    i++;
  }
  dict->is_initialized = true;
  json_object_put(o);
};

// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_file(AbbreviationDict *dict, fs::path abbrev_path) {
  json_object *o = json_object_from_file(abbrev_path.c_str());
  if (o == NULL) {
    die("unable to load abbreviations from file '%s'.\n", abbrev_path.c_str());
  }
  load_abbreviation_dict_from_json(dict, o);
};


/** tilde **/
namespace tilde {
  TildeView g_tilde;

  bool tildeWhenQuit(TildeView *tilde) {
    bool out = tilde->quitPressed;
    tilde->quitPressed = false;
    return out;
  };

  void tildeOpen(TildeView *tilde) {
    tilde->quitPressed = false;
    tilde->scrollback_ix = {};
  };

  void tildeHandleInput(TildeView *tilde, int c) {
      const int NSCREENROWS = 20;

    if (c == CTRL_KEY('c') || c == CTRL_KEY('q') || c == 'q' || c == '`' || c == '~') {
      tilde->quitPressed = true;
    } else if (c == CTRL_KEY('d')) {
        tilde->scrollback_ix = 
        clamp0u<int>(tilde->scrollback_ix + NSCREENROWS, tilde->log.size() - 1);;
    } else if (c == CTRL_KEY('u')) {
        tilde->scrollback_ix = clamp0(tilde->scrollback_ix - NSCREENROWS);
    } else if (c == CTRL_KEY('p') || c == 'k') {
        tilde->scrollback_ix = clamp0(tilde->scrollback_ix - 1);
    } else if (c == CTRL_KEY('n') || c == 'j') {
        tilde->scrollback_ix = clampu<int>(tilde->scrollback_ix + 1, tilde->log.size() - 1);
    } else if (c == 'g') {
      tilde->scrollback_ix = 0;
    } else if (c == 'G') {
      tilde->scrollback_ix = tilde->log.size() - 1;
    }
  }

  void tildeDraw(TildeView *tilde) {
    drawCallback([&](abuf &ab) {
      const int IX = tilde->scrollback_ix;
      assert(IX >= 0);
      if (tilde->log.size() > 0) {
        assert(IX < tilde->log.size());
      }
      ab.appendstr("~CONSOLE\x1b[K \r\n");

      static const int NDRAWCOLS = 118;
      static const int NDRAWROWS = 12;

      const interval drawRect = 
        interval(IX)
        .len_clampl_move_lr(NDRAWROWS)
        .clamp(0, tilde->log.size()-1);

      // assert(drawRect.l <= IX);
      // assert(IX <= drawRect.r);
      
      // TODO: draw per column.
      for(int r = drawRect.l; r <= drawRect.r; ++r) {
        ab.appendstr("\x1b[K \r\n");
        if (r == IX) { ab.appendstr(ESCAPE_CODE_CURSOR_SELECT); }
        ab.appendstr("▶  ");
        if (r == IX) { ab.appendstr(ESCAPE_CODE_UNSET); }

        abuf rowbuf = abuf::from_copy_str(tilde->log[r].c_str());
        for(int c = 0; c < rowbuf.ncodepoints().size; ++c) {
          if (c % NDRAWCOLS == NDRAWCOLS - 1) {
            ab.appendstr("\x1b[K \r\n");
            if (r == IX) { ab.appendstr(ESCAPE_CODE_CURSOR_SELECT); }
            ab.appendstr("   ┃");
            if (r == IX) { ab.appendstr(ESCAPE_CODE_UNSET); }
          }
          ab.appendCodepoint(rowbuf.getCodepoint(c));
        }
      }

    });
  };


  void tildeWrite(const char *fmt, ...) {
    if (!g_tilde.logfile) {
      g_tilde.logfile = fopen("/tmp/edtr-stderr", "w");
    }
    assert(g_tilde.logfile);

    // write arguments.
    va_list args;
    va_start(args, fmt);
    const int ERROR_LEN = 9000; 
    char *buf = (char*)calloc(sizeof(char), ERROR_LEN);
    vsnprintf(buf, ERROR_LEN, fmt, args);
    va_end(args);
    std::string str(buf);
    free(buf);
    g_tilde.log.push_back(str);
    str += "\n";
    fwrite(str.c_str(), 1, str.size(), g_tilde.logfile);
    fflush(g_tilde.logfile);



    // truncate log to `MAX_ENTRIES.
    const int MAX_ENTRIES = 100;
    const int nkeep = clampu<int>(MAX_ENTRIES, g_tilde.log.size());
    g_tilde.log.erase(g_tilde.log.begin(), g_tilde.log.begin() + nkeep);
    
    // scroll back upon writing.
    g_tilde.scrollback_ix = g_tilde.log.size() - 1;

  }

  void tildeWrite(const std::string &str) {
    tildeWrite("%s", str.c_str());
  };
  
  void tildeWrite(const abuf &buf) {
    tildeWrite(buf.to_std_string());
  }
  
};


/** ctrlp **/


// convert level 'quitPressed' into edge trigger.
bool ctrlpWhenQuit(CtrlPView *view) {
  const bool out = view->quitPressed;
  view->quitPressed = false;
  return out;
}
bool ctrlpWhenSelected(CtrlPView *view) {
  const bool out = view->selectPressed;
  view->selectPressed = false;
  return out;
}

FileLocation ctrlpGetSelectedFileLocation(const CtrlPView *view) {
  assert(view->absolute_cwd.is_absolute());
  assert(view->rgProcess.lines.size() > 0);
  assert(view->rgProcess.selectedLine >= 0);
  assert(view->rgProcess.selectedLine < view->rgProcess.lines.size());
  const abuf &line = view->rgProcess.lines[view->rgProcess.selectedLine];

  tilde::tildeWrite("line: ");
  tilde::tildeWrite(line);
    
  // search for the first `:`, that is going to be the file path.
  Size<Codepoint> colonOrEndIx = 0;
  for(; 
      ((colonOrEndIx < line.ncodepoints()) && 
       (*line.getCodepoint(colonOrEndIx) != ':'));
      ++colonOrEndIx) {}
  // TODO: check that std::string knows what to do when given two pointers.
  const fs::path relative_path_to_file(std::string(line.buf(), line.getCodepoint(colonOrEndIx)));
  const fs::path absolute_path_to_file = view->absolute_cwd / relative_path_to_file;

  tilde::tildeWrite(
    "absolute path '%s' | relative '%s' | found: '%s'", 
    view->absolute_cwd.c_str(),
    relative_path_to_file.c_str(), 
    absolute_path_to_file.c_str());
  
  Size<Codepoint> lineNumberOrEndIx = colonOrEndIx;
  for(; 
      ((lineNumberOrEndIx < line.ncodepoints()) && 
       (*line.getCodepoint(lineNumberOrEndIx) != ':'));
      ++lineNumberOrEndIx) {}

  int row = 0;  
  const std::string lineNumberStr(line.getCodepoint(colonOrEndIx), line.getCodepoint(lineNumberOrEndIx));
  if (lineNumberStr != "") {
    row = atoi(lineNumberStr.c_str()) - 1;
  }
  tilde::tildeWrite("lineNumberStr: %s | row: %d", lineNumberStr.c_str(), row);
  return FileLocation(absolute_path_to_file, Cursor(row, 0));
}


// places glob pattern after the parse.
//   . end of file.
//   . at sigil (@ or #).
const char SIGIL_GLOB_PATTERN_SEPARATOR = ','; 
const char SIGIL_FILE_DIRECTORY = '@'; // <email>@<provider.com> (where)
const char SIGIL_CONTENT_PATTERN = '#';

void CtrlPAssertNoWhitespace(const abuf &buf) {
  for(Ix<Byte> i(0); i < buf.nbytes(); ++i) {
    assert(buf.getByteAt(i) != ' ');
  } 
}

std::string CtrlPParseStringText(const abuf &buf, Ix<Byte> &i) {
  const Ix<Byte> begin = i;
  for(; i < buf.nbytes() && 
  	buf.getByteAt(i) != ',' && 
  	buf.getByteAt(i) != '#' && 
	  buf.getByteAt(i) != '@'; ++i) {  }
  // [begin, i)
  return std::string(buf.buf() + begin.ix, i.distance(begin));
}

// sepBy(GLOBPAT,",")("@"FILEPAT|"#"TEXTPAT|","GLOBPAT)*
enum RgPatKind {
  RPK_GLOB,
  RPK_DIR,
  RPK_TEXT,
};
CtrlPView::RgArgs CtrlPView::parseUserCommand(abuf buf) {
  CtrlPView::RgArgs args;
  RgPatKind k = RgPatKind::RPK_GLOB;
  CtrlPAssertNoWhitespace(buf);
  for(Ix<Byte> i(0); i < buf.nbytes(); i++) {
    // grab fragment.
    std::string fragment = CtrlPParseStringText(buf, i);
    // add fragment.
    if (fragment != "") {
      if (k == RPK_GLOB) { 
        args.pathGlobPatterns.push_back(fragment);
      } else if (k == RPK_DIR) {
        args.directoryPaths.push_back(fragment);
      } else if (k == RPK_TEXT) {
        if (args.fileContentPattern == "") {
          args.fileContentPattern = fragment;
        } else {
          args.fileContentPattern += "|";
          args.fileContentPattern += fragment;
        }
      } 
    } // if condition: empty fragment.

    // decide next fragment.
    if (buf.nbytes().isEnd(i)) { break; }
    else {
      const char sep = buf.getByteAt(i);
      // , separator, be glob pattern.
      if (sep == ',') { k = RPK_GLOB; }
      else if (sep == '@') { k = RPK_DIR; }
      else if (sep == '#') { k = RPK_TEXT; }
      else { 
        // someone wrote some malformed ripgrep. Ideally, this should never be allowed to be typed.
      }
    }
  }
  return args;
};


// rg TEXT_STRING PROJECT_PATH -g FILE_PATH_GLOB # find all files w/contents matching pattern.
// rg PROJECT_PATH --files -g FILE_PATH_GLOB # find all files w/ filename matching pattern.
std::vector<std::string> CtrlPView::rgArgsToCommandLineArgs(CtrlPView::RgArgs args) {
  // search for files
  std::vector<std::string> out;
  if (args.fileContentPattern != "") {
    // we are searching
    out.push_back(args.fileContentPattern);
  } else {
    out.push_back("--files"); // we are searching for files, no pattern.
  }

  // fill in search dirs.
  out.insert(out.end(), args.directoryPaths.begin(), args.directoryPaths.end());

  // fill in file glob pattern for file name.
  for(const std::string &s : args.pathGlobPatterns) {
    out.push_back("-g");
    out.push_back("*" + s + "*");
  }

  out.push_back("-S"); // smart case
  out.push_back("-n"); // line number.
  return out;

};

// compute a "good" path that Ctrlp should start from, based on heuristics. The heuristics are:
// if we find a `lakefile.lean`, that is a good root dir.
// if we find a `.git` folder, that is a good root dir.
// if we find a `.gitignore` folder, that is a good root dir.
// Consider grabbing the working directory with 'fs::absolute(fs::current_path())'
//   if no good starting path is known.
fs::path ctrlpGetGoodRootDirAbsolute(const fs::path absolute_startdir) {
  assert(absolute_startdir.is_absolute());
  std::optional<fs::path> out;
  if ((out = getFilePathAmongstParents(absolute_startdir, "lakefile.lean"))) {
    return out->parent_path();
  } else if ((out = getFilePathAmongstParents(absolute_startdir, ".git"))) {
    return out->parent_path();
  } else if ((out = getFilePathAmongstParents(absolute_startdir, ".gitignore"))) {
    return out->parent_path();
  } else {
    return absolute_startdir;
  }
}

void ctrlpOpen(CtrlPView *view, VimMode previous_state, fs::path absolute_cwd) {
  assert(absolute_cwd.is_absolute());
  view->absolute_cwd = absolute_cwd;
  view->textAreaMode = TAM_Insert;
  view->previous_state = previous_state;
  g_editor.vim_mode = VM_CTRLP;
}

void ctrlpHandleInput(CtrlPView *view, int c) {
  assert(view->textCol <= view->textArea.ncodepoints());
  if (view->textAreaMode == TAM_Normal) {
    if (c == 'j' || c == CTRL_KEY('n')) {
      view->rgProcess.selectedLine = 
      clamp0u<int>(view->rgProcess.selectedLine+1, view->rgProcess.lines.size()-1);
    } else if (c == 'k' || c == CTRL_KEY('p')) {
      view->rgProcess.selectedLine = 
          clamp0(view->rgProcess.selectedLine-1);
    } else if (c == 'h' || c == KEYEVENT_ARROW_LEFT) {
      view->textCol = view->textCol.sub0(1);
    } else if (c == 'l' || c == KEYEVENT_ARROW_RIGHT) {
      view->textCol = 
        clampu<Size<Codepoint>>(view->textCol + 1, view->textArea.ncodepoints());
    } else if (c == '$' || c == CTRL_KEY('e')) {
      view->textCol = view->textArea.ncodepoints();
    } else if (c == '0' || c == CTRL_KEY('a')) {
      view->textCol = 0;
    } else if (c == 'd' || c == CTRL_KEY('k')) {
      view->textArea.truncateNCodepoints(view->textCol);
    } else if (c == 'w') {
        // TODO: move word.
      view->textCol = 
        clampu<Size<Codepoint>>(view->textCol + 4, view->textArea.ncodepoints());
    } else if (c == 'x') {
      if (view->textCol < view->textArea.ncodepoints()) {
        view->textArea.delCodepointAt(view->textCol.toIx());
      }
    } 
    else if (c == 'b') {
      view->textCol = view->textCol.sub0(4);
      // TODO: move word back.
    } else if (c == 'i') {
      view->textAreaMode = TAM_Insert;
    }  else if (c == 'a') {
      view->textCol = 
        clampu<Size<Codepoint>>(view->textCol + 1, view->textArea.ncodepoints());
      view->textAreaMode = TAM_Insert;
    } else if (c == CTRL_KEY('c') || c == 'q') {
      // quit and go back to previous state.
      view->quitPressed = true;
    } else if (c == '\r') {
      // pressing <ENTER> either selects if a choice is available, or quits if no choices are available.
      if (view->rgProcess.lines.size() > 0) {
        view->selectPressed = true;
      } else {
        view->quitPressed = true;
      }
    }
 
  } else {
    assert(view->textAreaMode == TAM_Insert);
    if (c == CTRL_KEY('c')) {
      view->textAreaMode = TAM_Normal;
      // quit and go back to previous state.
    } else if (c == KEYEVENT_ARROW_LEFT || c == CTRL_KEY('b')) {
      view->textCol = view->textCol.sub0(1);
    } else if (c == KEYEVENT_ARROW_RIGHT || c == CTRL_KEY('f')) {
      view->textCol = 
        clampu<Size<Codepoint>>(view->textCol + 1, view->textArea.ncodepoints());
    } else if (c == KEYEVENT_BACKSPACE) {
      if (view->textCol > 0) {
        view->textArea.delCodepointAt(view->textCol.largestIx());
        view->textCol = view->textCol.sub0(1);
      } 
    } else if (c == CTRL_KEY('e')) { // readline
      view->textCol = view->textArea.ncodepoints();
    } else if (c == CTRL_KEY('a')) {
      view->textCol = 0;
    } else if (c == CTRL_KEY('k')) {
      view->textArea.truncateNCodepoints(view->textCol);
    } else if (c == CTRL_KEY('j') || c == CTRL_KEY('n')) {
      view->rgProcess.selectedLine = 
      clamp0u<int>(view->rgProcess.selectedLine+1, view->rgProcess.lines.size()-1);
    } else if (c == CTRL_KEY('k') || c == CTRL_KEY('p')) {
      view->rgProcess.selectedLine = 
          clamp0(view->rgProcess.selectedLine-1);
    } else if (isprint(c) && c != ' ' && c != '\t') {
      view->textArea.insertCodepointBefore(view->textCol, (const char *)&c);
      view->textCol += 1;
    }    else if (c == '\r') {
      view->selectPressed = true;
    }
  } 
}

void ctrlpDraw(CtrlPView *view) {
  abuf ab;
  if (view->textArea.whenDirty()) {
    // nuke previous rg process, and clear all data it used to own.
    view->rgProcess.killSync();
    view->rgProcess = RgProcess();
    // invoke the new rg process.
    CtrlPView::RgArgs args = CtrlPView::parseUserCommand(view->textArea);
    view->rgProcess.execpAsync(view->absolute_cwd.c_str(), 
      CtrlPView::rgArgsToCommandLineArgs(args));
  }


  // we need a function that is called each time x(.
  view->rgProcess.readLinesNonBlocking();

  { // draw text of the search.
    ab.appendstr("\x1b[?25l"); // hide cursor
    ab.appendstr("\x1b[2J");   // J: erase in display.
    ab.appendstr("\x1b[1;1H");  // H: cursor position

    // append format string.
    ab.appendfmtstr(120, "┎ctrlp mode (%s|%s|%d matches)┓\x1b[k\r\n", 
      textAreaModeToString(view->textAreaMode),
      view->rgProcess.running ? "running" : "completed",
      view->rgProcess.lines.size());

    const std::string cwd = std::string(view->absolute_cwd);
    ab.appendfmtstr(120, "searching: '%s'\x1b[k\r\n", cwd.c_str());
    
    const int NELLIPSIS = 2; // ellipsis width;
    const int VIEWSIZE = 80;
    const int NCODEPOINTS = view->textArea.ncodepoints().size;

    // interesting, see that the left and right actually appears to be well-typed.
    // NCODEPOINTS = 40. VIEWSIZE = 10
    // [0, 0] -> [-10, 10] -> [0, 10] -> [0, 10]
    // [1, 1] -> [-9, 11] -> [0, 11] -> [0, 10]?
    const auto intervalText = 
      interval(view->textCol.size)
      .ldl(VIEWSIZE)
      .clamp(0, NCODEPOINTS) // clamp length to be inbounds.
      .len_clampl_move_r(VIEWSIZE) // move right hand side point to be inbounds
      .clamp(0, NCODEPOINTS); // clamp right to be inbounds

    const auto intervalEllipsisL = interval(0, intervalText.l);
    for(int i = 0; i < NELLIPSIS; ++i) {
      ab.appendstr(ESCAPE_CODE_DULL);
      ab.appendstr(i < intervalEllipsisL.r ?  "«" : " ");
      ab.appendstr(ESCAPE_CODE_UNSET);
    }
    ab.appendstr(" ");

    // TODO: why does the right hand size computation not work?
    for(int i = intervalText.l; i <= intervalText.r; ++i) {
      appendColWithCursor(&ab, &view->textArea, i, view->textCol, view->textAreaMode);
    }

    ab.appendstr(" ");

    const auto intervalEllipsisR = interval(intervalText.r, NCODEPOINTS).lregauge();
    for(int i = 0; i < NELLIPSIS; ++i) {
      ab.appendstr(ESCAPE_CODE_DULL);
      ab.appendstr(i < intervalEllipsisR.r ?  "»" : " ");
      ab.appendstr(ESCAPE_CODE_UNSET);
    }
    ab.appendstr("\x1b[K\r\n");
  }

  // ab.appendstr(ESCAPE_CODE_UNSET);
  ab.appendstr("\x1b[K\r\n");
  ab.appendstr("━━━━━\x1b[K\r\n");

  { // draw text from rg.
    const int VIEWSIZE = 80;
    const int NELLIPSIS = 3;
    const int NROWS = g_editor.screenrows - 5;

    for(int i = 0; i < view->rgProcess.lines.size() && i < NROWS; ++i) {
      const abuf &line = view->rgProcess.lines[i];
      const int NCODEPOINTS = line.ncodepoints().size;

      if (i == view->rgProcess.selectedLine) {
        ab.appendstr(ESCAPE_CODE_CURSOR_SELECT);
      }

      const interval intervalText = interval(0, VIEWSIZE - NELLIPSIS).clamp(0, NCODEPOINTS);
      for(int i = intervalText.l; i < intervalText.r; ++i) {
        ab.appendCodepoint(line.getCodepoint(Ix<Codepoint>(i)));
      }

      const interval intervalEllipsisR = interval(intervalText.r, NCODEPOINTS).lregauge();
      for(int i = 0; i < 2; ++i) {
        ab.appendstr(ESCAPE_CODE_DULL);
        ab.appendstr(i < intervalEllipsisR.r ?  "»" : " ");
        ab.appendstr(ESCAPE_CODE_UNSET);

      }

      if (i == view->rgProcess.selectedLine) {
        ab.appendstr(ESCAPE_CODE_UNSET);
      }

      ab.appendstr("\x1b[K \r\n");
    } // end loop for drawing rg.
  }
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}

/*
// TODO: figure out how to implement this right!
bool RgProcess::isRunningNonBlocking() {
  if (!this->running) { return false; }
  int status;
  int ret = waitpid(this->childpid, &status, WNOHANG);
  if (ret == 0) {
    // status has not changed.
    return true; // process is still running.
  } else if (ret == -1) {
    perror("waitpid threw error in monitoring `rg` process.");
    die("waitpid threw error in monitoring `rg`!");
  } else {
    assert(ret > 0);
    this->running = false;
  }
  return this->running;
}
*/

// (re)start the process, and run it asynchronously.
void RgProcess::execpAsync(const char *working_dir, std::vector<std::string> args) {
  this->lines = {};
  this->selectedLine = -1;

  assert(!this->running);
  CHECK_POSIX_CALL_0(pipe2(this->child_stdout_to_parent_buffer, O_NONBLOCK));

  this->childpid = fork();
  if(childpid == -1) {
    perror("ERROR: fork failed.");
    exit(1);
  };

  if(childpid == 0) {
    close(STDIN_FILENO);
    close(STDERR_FILENO);
    // child->parent, child will only write to this pipe, so close read end.
    close(this->child_stdout_to_parent_buffer[PIPE_READ_IX]);
    // it is only legal to call `write()` on stdout. So we tie the `PIPE_WRITE_IX` to `STDOUT`
    dup2(this->child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO);

    if (chdir(working_dir) != 0) {
      die("ERROR: unable to run `rg, cannot switch to working directory '%s'", working_dir);
    }; 
    const char * process_name = "rg";
    // process_name, arg1, ..., argN, NULL
    char **argv = (char **) calloc(sizeof(char*), args.size() + 2);
    argv[0] = strdup(process_name);
    for(int i = 0; i < args.size(); ++i) {
      argv[1 + i] = strdup(args[i].c_str()); 
    }
    argv[1+args.size()] = NULL;
    if (execvp(process_name, argv) == -1) {
      perror("failed to launch ripgrep");
      abort();
    }
  } else {

    // parent<-child, parent will only read from this pipe, so close write end.
    close(this->child_stdout_to_parent_buffer[PIPE_WRITE_IX]);

    // parent.
    assert(this->childpid != 0);
    this->running = true;
  }
};

// kills the process synchronously.
void RgProcess::killSync() {
  if (!this->running) { return; }
  kill(this->childpid, SIGKILL);
  this->running = false;
}


int RgProcess::_read_stdout_str_from_child_nonblocking() {
  const int BUFSIZE = 4096;
  char buf[BUFSIZE];
  int nread = read(this->child_stdout_to_parent_buffer[PIPE_READ_IX], buf, BUFSIZE);
  if (nread == -1) {
    if (errno == EAGAIN) {
      // EAGAIN = non-blocking I/O, there was no data ro read.
      return 0;
    }
    die("unable to read from stdout of child lean server");
  }
  this->child_stdout_buffer.appendbuf(buf, nread);
  return nread;
};

// return true if line was read.
bool RgProcess::readLineNonBlocking() {
  int newline_ix = 0;
  for(; newline_ix < this->child_stdout_buffer.len(); newline_ix++) {
    if (this->child_stdout_buffer.getByteAt(Ix<Byte>(newline_ix)) == '\n') {
      break;
    }
  }
  if (newline_ix >= this->child_stdout_buffer.len()) {
    return false;
  }
  assert (newline_ix < this->child_stdout_buffer.len());
  assert(child_stdout_buffer.getByteAt(Ix<Byte>(newline_ix)) == '\n');
  // if we find 'a\nbc' (newline_ix=1) we want to:
  //   . take the string 'a' (1).
  //   . drop the string 'a\n' (2).
  this->lines.push_back(this->child_stdout_buffer.takeNBytes(newline_ix));
  if (this->selectedLine == -1) { this->selectedLine = 0; }
  child_stdout_buffer.dropNBytesMut(newline_ix+1);
  return true;
}

int RgProcess::readLinesNonBlocking() {
  _read_stdout_str_from_child_nonblocking();
  int nlines = 0;
  while(this->readLineNonBlocking()) {
    nlines++;
  }
  return nlines;
}
