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

// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
// [(<accent>;)?<forground>;<background>;
#define ESCAPE_CODE_DULL "\x1b[90;40m" // briht black foreground, black background
#define ESCAPE_CODE_CURSOR_INSERT "\x1b[30;47m" // black foreground, white background
#define ESCAPE_CODE_CURSOR_NORMAL "\x1b[30;100m" // black foreground, bright black background
#define ESCAPE_CODE_CURSOR_SELECT "\x1b[30;43m" // black foreground, yellow background
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
void drawColWithCursor(abuf *dst, const abuf *row, 
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
void _exec_lean_server_on_child(char *lakefile_dirpath) {
  int process_error = 0;
  // no lakefile.
  if (lakefile_dirpath == NULL) {
    fprintf(stderr, "starting 'lean --server'...\n");
    const char *process_name = "lean";
    char * const argv[] = { strdup(process_name), strdup("--server"), NULL };
    process_error = execvp(process_name, argv);
  } else {
    if (chdir(lakefile_dirpath) != 0) {
      die("ERROR: unable to switch to 'lakefile.lean' directory");
    }; 
    free(lakefile_dirpath);
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
char *get_lakefile_dirpath(const char *file_path) {
  assert(file_path);
  // walk up directory tree of 'file_path' in a loop, printing parents,
  // until we hit the root directory, then stop.
  char *dirpath = strdup(file_path);
  dirpath = strdup(dirname(dirpath));
  
  int prev_inode = -1; // inode of child. if it equals inode of parent, then we have hit /. 

  // only iterate this loop a bounded number of times.
  int NUM_PARENT_DIRS_TO_WALK = 1000;
  bool hit_root = false;
  for(int i = 0; i < NUM_PARENT_DIRS_TO_WALK && !hit_root; ++i) {
    assert(i != NUM_PARENT_DIRS_TO_WALK - 1 && 
      "ERROR: recursing when walking up parents to find `lakefile.lean`.");
    DIR *dir = opendir(dirpath);
    assert(dir && "unable to open directory");
    dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
      const char *file_name = entry->d_name;
      if (strcmp(file_name, ".") == 0) {
        const int cur_inode = entry->d_ino;
        hit_root = hit_root || (cur_inode == prev_inode);
        prev_inode = cur_inode;
      }

      if (strcmp(file_name, "lakefile.lean") == 0) {
        char *realpath_dirpath = realpath(dirpath, NULL);
        free(dirpath);
        return realpath_dirpath;
      }
    } // end readdir() while loop.
    closedir(dir);

    // walk up parent, free memory.
    const char *parent_dot_dot_slash = "/../";
    char *parent_path = (char *)calloc(strlen(dirpath) + strlen(parent_dot_dot_slash) + 1,
      sizeof(char));
    sprintf(parent_path, "%s%s", dirpath, parent_dot_dot_slash);
    free(dirpath);
    dirpath = parent_path;
  } // end for loop over directory parents.
  
  return NULL;
}


// create a new lean server.
// if file_path == NULL, then create `lean --server`.
LeanServerState LeanServerState::init(const char *file_path) {
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

  if (file_path) {
    char *lakefile_dirpath = get_lakefile_dirpath(file_path);
    fprintf(stderr, "lakefile_dirpath: '%s'\n", lakefile_dirpath);
    free(lakefile_dirpath);
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

    // it is only legal to call `write()` on stdout. So we tie the `PIPE_WRITE_IX` to `STDIN`
    dup2(state.child_stderr_to_parent_buffer[PIPE_WRITE_IX], STDERR_FILENO);
    // it is only legal to call `write()` on stdout. So we tie the `PIPE_WRITE_IX` to `STDIN`
    dup2(state.child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO);
    // it is only legal to call `read()` on stdin. So we tie the `PIPE_READ_IX` to `STDIN`
    dup2(state.parent_buffer_to_child_stdin[PIPE_READ_IX], STDIN_FILENO);
    if (file_path) {
      // we have a file that we want to find the lakefile for.
      char *lakefile_dirpath = get_lakefile_dirpath(file_path);
      _exec_lean_server_on_child(lakefile_dirpath);
    } else {
      _exec_lean_server_on_child(NULL);
    }

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
  free(request_str);
}

// tries to read the next JSON record from the buffer, in a nonblocking fashion.
json_object *LeanServerState::_read_next_json_record_from_buffer_nonblocking() {

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
  child_stdout_buffer.drop_prefix(header_line_end_ix + content_length);

  return o;
}

// tries to read the next JSON record from the buffer, in a blocking fashion.
// It busy waits on the nonblocking version.
json_object *LeanServerState::_read_next_json_record_from_buffer_blocking() {
  while(1) {
    json_object *o = _read_next_json_record_from_buffer_nonblocking(); 
    if (o) { return o; }
    _read_stdout_str_from_child_nonblocking(); // consume input to read.
  }
}

json_object *LeanServerState::read_json_response_from_child_blocking(LspRequestId request_id) {
  assert(request_id.id < this->next_request_id); // check that it is a valid request ID.

  // TODO: the request needs to be looked for in the vector of unprocessed requests.
  while(1) {
    assert(this->nresponses_read < this->next_request_id);
    json_object *o = _read_next_json_record_from_buffer_blocking();
    // only records with a key called "id" are responses.
    // other records are status messages which we silently discard
    // TODO: do not silently discard!
    json_object *response_id = NULL;
    if (json_object_object_get_ex(o, "id", &response_id) && 
        json_object_get_type(response_id) == json_type_int) {
        if (json_object_get_int(response_id) != request_id.id) {
          // TODO: store responses in a queue.
          assert(false && "unexpected response, got a non-matching response to the ID we wanted.");
        }
        this->nresponses_read++;
        return o;
    } else {
        this->unhandled_server_requests.push_back(o);
    }
  }
}

/*** defines ***/

void editorSetStatusMessage(const char *fmt, ...);


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

  f->rows.push_back(FileRow()); 
  for(int i = f->rows.size() - 1; i >= at + 1; i--)  {
    f->rows[i] = f->rows[i - 1];   
  }
  f->rows[at].setBytes(s, len, g_editor.curFile);
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
    FileRow *row = &f->rows[f->cursor.row];
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
    row->truncateNCodepoints(Size<Codepoint>(f->cursor.col), g_editor.curFile);
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

  FileRow *row = &f->rows[f->cursor.row];

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
        row->delCodepointAt(Ix<Codepoint>(f->cursor.col.largestIx()), g_editor.curFile);
        f->cursor.col--;
      }

      // TODO: check if we have `toInserts` that are more than 1 codepoint.
      const char *toInsert = g_editor.abbrevDict.abbrevs[info.matchix];
        row->insertCodepointBefore(f->cursor.col, toInsert, g_editor.curFile);
      f->cursor.col++;
    }
  }
  row->insertByte(f->cursor.col, c, g_editor.curFile);  
  f->cursor.col++; // move cursor.
}

// TODO: come up with the correct abstractions for these.
// run the X command in vim.
void fileConfigXCommand(FileConfig *f) {
  f->makeDirty();
  if (f->cursor.row == f->rows.size()) { return; }

  FileRow *row = &f->rows[f->cursor.row];

  // nothing under cursor.
  if (f->cursor.col == row->ncodepoints()) { return; }
  // delete under the cursor.
  row->delCodepointAt(f->cursor.col.toIx(), g_editor.curFile);
}

// TODO: study this and make a better abstraction for the cursor location.
void fileConfigBackspace(FileConfig *f) {
  f->makeDirty();
  if (f->cursor.row == f->rows.size()) {
    return;
  }
  if (f->cursor.col == Size<Codepoint>(0) && f->cursor.row == 0)
    return;

  FileRow *row = &f->rows[f->cursor.row];

  // if col > 0, then delete at cursor. Otherwise, join lines toegether.
  if (f->cursor.col > Size<Codepoint>(0)) {
    // delete at the cursor.
    row->delCodepointAt(f->cursor.col.largestIx(), g_editor.curFile);
    f->cursor.col--;
  } else {
    // place cursor at last column of prev row.
    f->cursor.col = f->rows[f->cursor.row - 1].ncodepoints();
    // append string.
    for(Ix<Codepoint> i(0); i < row->ncodepoints(); ++i) {
      f->rows[f->cursor.row - 1].appendCodepoint(row->getCodepoint(i), g_editor.curFile);
    }
    // delete current row
    fileConfigDelRow(f, f->cursor.row);
    // go to previous row.
    f->cursor.row--;
  }
}


void fileConfigLaunchLeanServer(FileConfig *file_config) {


  assert(file_config->absolute_filepath != NULL);
  assert(file_config->lean_server_state.initialized == false);
  file_config->lean_server_state = LeanServerState::init(file_config->absolute_filepath); // start lean --server.  
  // file_config->lean_server_state = LeanServerState::init(NULL); // start lean --server.  

  json_object *req = lspCreateInitializeRequest();
  LspRequestId request_id = file_config->lean_server_state.write_request_to_child_blocking("initialize", req);
  json_object_put(req);

  json_object *response = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

  // initialize: send initialized
  req = lspCreateInitializedNotification();
  file_config->lean_server_state.write_notification_to_child_blocking("initialized", req);

}

void fileConfigSyncLeanState(FileConfig *file_config) {
  json_object *req = nullptr;
  assert(file_config->is_initialized);
  if (file_config->text_document_item.is_initialized && !file_config->is_dirty) {
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
    assert(buf.len() > 0);
    assert(buf.buf()[buf.len() - 1] == 0); // must be null-termianted.
    file_config->text_document_item.text = strdup(buf.buf());
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
  if (file_config->leanInfoViewPlainGoal) {
    // json_object_put(file_config->leanInfoViewPlainTermGoal);
    file_config->leanInfoViewPlainGoal = nullptr;
  }

  assert(file_config->leanInfoViewPlainGoal == nullptr);


  // TODO: need to convert col to 'bytes'
  Size<Byte> cursorColBytes;
  if (file_config->cursor.row < file_config->rows.size())  {
    cursorColBytes = file_config->rows[file_config->cursor.row].getBytesTill(file_config->cursor.col);
  } else {
    // cursorColBytes = 0.
    cursorColBytes = Size<Byte>(0);
  }
  req = lspCreateLeanPlainGoalRequest(file_config->text_document_item.uri, 
    Position(file_config->cursor.row, cursorColBytes.size));
  request_id = file_config->lean_server_state.write_request_to_child_blocking("$/lean/plainGoal", req);
  file_config->leanInfoViewPlainGoal = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

  // $/lean/plainTermGoal
  if (file_config->leanInfoViewPlainTermGoal) {
    // json_object_put(file_config->leanInfoViewPlainTermGoal);
    file_config->leanInfoViewPlainTermGoal = nullptr;
  }

  assert(file_config->leanInfoViewPlainTermGoal == nullptr);
  req = lspCreateLeanPlainTermGoalRequest(file_config->text_document_item.uri, 
    Position(file_config->cursor.row, cursorColBytes.size));
  request_id = file_config->lean_server_state.write_request_to_child_blocking("$/lean/plainTermGoal", req);
  file_config->leanInfoViewPlainTermGoal = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

  // textDocument/hover
  if (file_config->leanHoverViewHover) {
    // json_object_put(file_config->leanInfoViewPlainTermGoal);
    file_config->leanHoverViewHover = nullptr;
  }

  assert(file_config->leanHoverViewHover == nullptr);
  req = lspCreateTextDocumentHoverRequest(file_config->text_document_item.uri, 
    Position(file_config->cursor.row, cursorColBytes.size));
  request_id = file_config->lean_server_state.write_request_to_child_blocking("textDocument/hover", req);
  file_config->leanHoverViewHover = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

}


/*** file i/o ***/
void fileConfigOpen(FileConfig *f, const char *filename) {
  free(f->absolute_filepath);
  f->absolute_filepath = strdup(filename);

  FILE *fp = fopen(filename, "a+");
  if (!fp) {
    die("fopen");
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
    fileConfigInsertRowBefore(f, f->rows.size(), line, linelen);
  }
  free(line);
  fclose(fp);
  f->is_initialized = true;
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
  if (f->absolute_filepath == NULL || !f->is_dirty) {
    return;
  }
  abuf buf;
  fileConfigRowsToBuf(&g_editor.curFile, &buf);
  // | open for read and write
  // | create if does not exist
  // 0644: +r, +w
  int fd = open(f->absolute_filepath, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
  }
  assert(fd != -1 && "unable to open file");
  // | set file to len.
  int err = ftruncate(fd, buf.len());
  assert(err != -1 && "unable to truncate");
  int nwritten = write(fd, buf.buf(), buf.len());
  assert(nwritten == buf.len() && "wasn't able to write enough bytes");
  editorSetStatusMessage("Saved file");
  f->is_dirty = false;
  close(fd);
}


/*** append buffer ***/

/*** output ***/

void editorScroll() {
  g_editor.curFile.cursor_render_col = 0;
  assert (g_editor.curFile.cursor.row >= 0 && g_editor.curFile.cursor.row <= g_editor.curFile.rows.size());
  if (g_editor.curFile.cursor.row < g_editor.curFile.rows.size()) {
    g_editor.curFile.cursor_render_col = 
      g_editor.curFile.rows[g_editor.curFile.cursor.row].cxToRx(Size<Codepoint>(g_editor.curFile.cursor.col));
  }
  if (g_editor.curFile.cursor.row < g_editor.curFile.scroll_row_offset) {
    g_editor.curFile.scroll_row_offset = g_editor.curFile.cursor.row;
  }
  if (g_editor.curFile.cursor.row >= g_editor.curFile.scroll_row_offset + g_editor.screenrows) {
    g_editor.curFile.scroll_row_offset = g_editor.curFile.cursor.row - g_editor.screenrows + 1;
  }
  if (g_editor.curFile.cursor_render_col < g_editor.curFile.scroll_col_offset) {
    g_editor.curFile.scroll_col_offset = g_editor.curFile.cursor_render_col;
  }
  if (g_editor.curFile.cursor_render_col >= g_editor.curFile.scroll_col_offset + g_editor.screencols) {
    g_editor.curFile.scroll_col_offset = g_editor.curFile.cursor_render_col - g_editor.screencols + 1;
  }
}

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


void editorDrawRows(abuf &ab) {
  // When we print the line number, tilde, we then print a
  // "\r\n" like on any other line, but this causes the terminal to scroll in
  // order to make room for a new, blank line. Let’s make the last line an
  // exception when we print our
  // "\r\n".

  // plus one at the end for the pipe, and +1 on the num_digits so we start from '1'.
  const int LINE_NUMBER_NUM_CHARS = num_digits(g_editor.screenrows + g_editor.curFile.scroll_row_offset + 1) + 1;
  for (int row = 0; row < g_editor.screenrows; row++) {
    int filerow = row + g_editor.curFile.scroll_row_offset;

    // convert the line number into a string, and write it.
    {
      // code in view mode is renderered gray
      if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr(ESCAPE_CODE_DULL); }

      char *line_number_str = (char *)calloc(sizeof(char), (LINE_NUMBER_NUM_CHARS + 1)); // TODO: allocate once.
      int ix = write_int_to_str(line_number_str, filerow + 1);
      while(ix < LINE_NUMBER_NUM_CHARS - 1) {
        line_number_str[ix] = ' ';
        ix++;
      }
      line_number_str[ix] = '|';
      ab.appendstr(line_number_str);
      free(line_number_str);

      // code in view mode is renderered gray, so reset.
      if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr("\x1b[0m"); }

    }
    // code in view mode is renderered gray
    if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr("\x1b[37;40m"); }

    const TextAreaMode textAreaMode = 
      g_editor.vim_mode == VM_NORMAL ? TextAreaMode::TAM_Normal : TAM_Insert;

    if (filerow < g_editor.curFile.rows.size()) {
      const FileRow &row = g_editor.curFile.rows[filerow];
      const Size<Codepoint> NCOLS = 
        clampu<Size<Codepoint>>(row.ncodepoints(), g_editor.screencols - LINE_NUMBER_NUM_CHARS - 1);
      assert(g_editor.vim_mode == VM_NORMAL || g_editor.vim_mode == VM_INSERT);

      if (filerow == g_editor.curFile.cursor.row) {
        for(Ix<Codepoint> i(0); i < NCOLS; ++i) {
          drawColWithCursor(&ab, row, i, g_editor.curFile.cursor.col, textAreaMode);
        }
      }
    } else if (filerow == g_editor.curFile.rows.size() && g_editor.curFile.cursor.row == filerow) {
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
}

void editorDrawNormalInsertMode() {

  // initEditor();
  editorScroll();
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

  editorDrawRows(ab);
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}


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

bool isInfoViewTacticsHoverTabEnabled() {
  json_object  *result = nullptr;
  json_object_object_get_ex(g_editor.curFile.leanHoverViewHover, "result", &result);
  return result != nullptr; 
}

bool isInfoViewTacticsTacticsTabEnabled() {
  json_object  *result = nullptr;
  json_object_object_get_ex(g_editor.curFile.leanInfoViewPlainTermGoal, "result", &result);
  if (result != nullptr) { return true; }; 
  
  json_object_object_get_ex(g_editor.curFile.leanInfoViewPlainGoal, "result", &result);
  if (result != nullptr) { return true; }; 
  return false;
}

void editorDrawInfoViewTacticsTabbar(InfoViewTab tab, abuf *buf) {
    assert(tab >= 0 && tab < IVT_NumTabs);
    const char *str[IVT_NumTabs] = {"Tactics", "Hover","Messages"};    
    bool enabled[IVT_NumTabs] = {isInfoViewTacticsTacticsTabEnabled(), isInfoViewTacticsHoverTabEnabled(), true};
    // TODO: add a concept of a tab being enabled.

    for(int i = 0; i < IVT_NumTabs; ++i) {
      if (i == (int) tab) {
        buf->appendstr("\x1b[1;97m");
      } else {
        buf->appendstr(ESCAPE_CODE_DULL);
      }
      buf->appendstr("┎");
      if (i == (int) tab) {
        buf->appendstr("■ ");
      } else if (!enabled[i]) {
        buf->appendstr("♯ "); // disabled.
      } else {
        buf->appendstr("□ ");
      }
      
      buf->appendstr(str[i]);
      buf->appendstr("┓");

      buf->appendstr("\x1b[0m");

    }
    buf->appendstr("\r\n");
}

void editorDrawInfoViewTacticsTab() {
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
  // ab.appendstr("@@@ LEAN INFOVIEW @@@ \r\n");
  editorDrawInfoViewTacticsTabbar(IVT_Tactic, &ab);
  assert(g_editor.curFile.leanInfoViewPlainGoal);
  assert(g_editor.curFile.leanInfoViewPlainTermGoal);
  // ab.appendfmtstr(120, "gl: %s \x1b[K \r\n", 
  //   json_object_to_json_string_ext(g_editor.curFile.leanInfoViewPlainGoal, JSON_C_TO_STRING_NOSLASHESCAPE));
  // ab.appendfmtstr(120, "trmgl: %s \x1b[K \r\n", 
  //   json_object_to_json_string_ext(g_editor.curFile.leanInfoViewPlainTermGoal, JSON_C_TO_STRING_NOSLASHESCAPE));

  do {
    json_object  *result = nullptr;
    json_object_object_get_ex(g_editor.curFile.leanInfoViewPlainTermGoal, "result", &result);
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
    json_object_object_get_ex(g_editor.curFile.leanInfoViewPlainGoal, "result", &result);
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

void editorDrawInfoViewMessagesTab() {
  abuf ab;

  ab.appendstr("\x1b[?25l"); // hide cursor
  ab.appendstr("\x1b[2J");   // J: erase in display.
  ab.appendstr("\x1b[1;1H");  // H: cursor position
  editorDrawInfoViewTacticsTabbar(IVT_Messages, &ab);
  ab.appendstr("\x1b[K"); // The K command (Erase In Line) erases part of the current line.
  ab.appendstr("\r\n");  // always append a space



  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));

}


void editorDrawInfoViewHoverTab() {
  abuf ab;

  ab.appendstr("\x1b[?25l"); // hide cursor
  ab.appendstr("\x1b[2J");   // J: erase in display.
  ab.appendstr("\x1b[1;1H");  // H: cursor position
  editorDrawInfoViewTacticsTabbar(IVT_Hover, &ab);
  ab.appendstr("\x1b[K"); // The K command (Erase In Line) erases part of the current line.
  ab.appendstr("\r\n");  // always append a space

  do {
    json_object  *result = nullptr;
    json_object_object_get_ex(g_editor.curFile.leanHoverViewHover, "result", &result);
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


InfoViewTab infoViewTabCycleNext(InfoViewTab t) {
  bool enabled[IVT_NumTabs] = {isInfoViewTacticsTacticsTabEnabled(), isInfoViewTacticsHoverTabEnabled(), true};
  bool foundEnabled = false;
  t = InfoViewTab(((int) t + 1)  % IVT_NumTabs);
  for(int i = 0; i < IVT_NumTabs; ++i) {
    if (enabled[t]) {
      foundEnabled = true;
      break;
    } else {
      t = InfoViewTab(((int) t + 1) % IVT_NumTabs);
    }
  }
  assert(foundEnabled && "unable to find any enabled tab on the info view.");
  return t;
}



void editorDrawInfoView() {
  bool enabled[IVT_NumTabs] = {isInfoViewTacticsTacticsTabEnabled(), isInfoViewTacticsHoverTabEnabled(), true};
  bool foundEnabled = false;
  for(int i = 0; i < IVT_NumTabs; ++i) {
    if (enabled[g_editor.curFile.infoViewTab]) {
      foundEnabled = true;
      break;
    } else {
      g_editor.curFile.infoViewTab = InfoViewTab((int(g_editor.curFile.infoViewTab) + 1) % IVT_NumTabs);
    }
  }
  assert(foundEnabled && "unable to find any enabled tab on the info view.");

  if(g_editor.curFile.infoViewTab <= IVT_Tactic && 
        isInfoViewTacticsTacticsTabEnabled()) {
      editorDrawInfoViewTacticsTab();
    return;
  }

  if(g_editor.curFile.infoViewTab <= IVT_Hover &&
        isInfoViewTacticsHoverTabEnabled()) {
      editorDrawInfoViewHoverTab();
      return;
  }

  if (g_editor.curFile.infoViewTab <= IVT_Messages) {
    editorDrawInfoViewMessagesTab();
    return;
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

void editorDrawCtrlPMode() {
  abuf ab;
  ctrlpDraw(&g_editor.curFile.ctrlp, &ab);
  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
}

void editorDraw() {
  if (g_editor.vim_mode == VM_NORMAL || g_editor.vim_mode == VM_INSERT) {
    editorDrawNormalInsertMode();
    return;
  } else if (g_editor.vim_mode == VM_COMPLETION) {
    editorDrawCompletionMode();
  } else if (g_editor.vim_mode == VM_CTRLP) {
    editorDrawCtrlPMode();
  } else {
    assert(g_editor.vim_mode == VM_INFOVIEW_DISPLAY_GOAL);
    editorDrawInfoView();
  }

}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_editor.statusmsg, sizeof(g_editor.statusmsg), fmt, ap);
  va_end(ap);
  g_editor.statusmsg_time = time(NULL);
}

/*** input ***/

// row can be `NULL` if one wants to indicate the last, sentinel row.
/*
RowMotionRequest fileRowMoveCursorIntraRow(Cursor *cursor, FileRow *row, LRDirection dir) {
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
  FileRow *row = &f->rows[f->cursor.row];

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
  FileRow *row = &f->rows[f->cursor.row];

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
    FileRow *row = &f->rows[f->cursor.row]; 
    while(row->ncodepoints() > f->cursor.col) {
        row->delCodepointAt(row->ncodepoints().largestIx(), *f);
    }
  }
}


void editorMoveCursor(int key) {
  switch (key) {
  case 'w': {
    fileConfigCursorMoveWordNext(&g_editor.curFile);
    break;
  }
  case 'b': {
    fileConfigCursorMoveWordPrev(&g_editor.curFile);
    break;
  }

  case 'h': {
    fileConfigCursorMoveCharLeft(&g_editor.curFile);
    break;
  }
  case 'l':
    fileConfigCursorMoveCharRight(&g_editor.curFile);
    break;
  case 'k':
    if (g_editor.curFile.cursor.row > 0) {
      g_editor.curFile.cursor.row--;
    }
    g_editor.curFile.cursor.col = 
      std::min<Size<Codepoint>>(g_editor.curFile.cursor.col, g_editor.curFile.rows[g_editor.curFile.cursor.row].ncodepoints());
    break;
  case 'j':
    if (g_editor.curFile.cursor.row < g_editor.curFile.rows.size()) {
      g_editor.curFile.cursor.row++;
    }
    if (g_editor.curFile.cursor.row < g_editor.curFile.rows.size()) {
      g_editor.curFile.cursor.col = 
        std::min<Size<Codepoint>>(g_editor.curFile.cursor.col, g_editor.curFile.rows[g_editor.curFile.cursor.row].ncodepoints());
    }
    break;
  case CTRL_KEY('d'):
    g_editor.curFile.cursor.row = clampu<int>(g_editor.curFile.cursor.row + g_editor.screenrows / 4, g_editor.curFile.rows.size());
    if (g_editor.curFile.cursor.row < g_editor.curFile.rows.size()) {
      g_editor.curFile.cursor.col = 
        std::min<Size<Codepoint>>(g_editor.curFile.cursor.col, g_editor.curFile.rows[g_editor.curFile.cursor.row].ncodepoints());
    }
    break;
  case CTRL_KEY('u'):
    g_editor.curFile.cursor.row = clamp0<int>(g_editor.curFile.cursor.row - g_editor.screenrows / 4);
    g_editor.curFile.cursor.col = 
      std::min<Size<Codepoint>>(g_editor.curFile.cursor.col, g_editor.curFile.rows[g_editor.curFile.cursor.row].ncodepoints());
    break;
  }
}


int editorReadRawEscapeSequence() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
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
    fileConfigInsertRowBefore(&g_editor.curFile, f->cursor.row, nullptr, 0);
  } else {
    fileConfigInsertRowBefore(&g_editor.curFile, f->cursor.row + 1, nullptr, 0);
    f->cursor.row += 1;
    f->cursor.col = Size<Codepoint>(0);
  }
};

// open row above ('O');
void fileConfigOpenRowAbove(FileConfig *f) {
  fileConfigInsertRowBefore(&g_editor.curFile, 0, nullptr, 0);
}

void editorProcessKeypress() {
  int nread;
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
  else if (g_editor.vim_mode == VM_CTRLP) {
    ctrlpHandleInput(&g_editor.curFile.ctrlp, c);
    if (ctrlpWhenQuit(&g_editor.curFile.ctrlp)) {
      g_editor.vim_mode = g_editor.curFile.ctrlp.previous_state;
      return;
    }
  }
  else if (g_editor.vim_mode == VM_INFOVIEW_DISPLAY_GOAL) { // behaviours only in infoview mode
    switch(c) {
    case 'q': {
      fileConfigSave(&g_editor.curFile);
      exit(0);
      return;
    }
    case '\t': {
      // TODO: this is not per file info? or is it?
      g_editor.curFile.infoViewTab = infoViewTabCycleNext(g_editor.curFile.infoViewTab);
      return;
    }
    case CTRL_KEY('c'):
    case 'g':
    case ' ':
    case '?': {
      g_editor.vim_mode = VM_NORMAL; return;
    }
    default: {
      return;
    }
    }
  }
  else if (g_editor.vim_mode == VM_NORMAL) { // behaviours only in normal mode
    switch (c) {
    case CTRL_KEY('p'): {
      g_editor.curFile.ctrlp.previous_state = VM_NORMAL;
      g_editor.vim_mode = VM_CTRLP;
      return;
    }
    case 'D': {
      g_editor.curFile.mkUndoMemento();
      fileConfigDeleteTillEndOfRow(&g_editor.curFile);
      return;
    }
    case 'q': {
      fileConfigSave(&g_editor.curFile);
      die("bye!");
      return;
    }
    case 'u': {
      g_editor.curFile.doUndo();
      return;
    }
    case 'r':
    case 'U': {
      g_editor.curFile.doRedo();
      return;

    }
    case 'x': {
      g_editor.curFile.mkUndoMemento();
      fileConfigXCommand(&g_editor.curFile);
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
      editorMoveCursor(c);
      break;
    }
    case 'o': {
      g_editor.curFile.mkUndoMemento();
      fileConfigOpenRowBelow(&g_editor.curFile);
      g_editor.vim_mode = VM_INSERT;
      return;
    }
    case 'O': {
      g_editor.curFile.mkUndoMemento();
      fileConfigOpenRowAbove(&g_editor.curFile);
      g_editor.vim_mode = VM_INSERT;
      return; 
    }

    case 'a': {
      g_editor.curFile.mkUndoMemento();
      fileConfigCursorMoveCharRightNoWraparound(&g_editor.curFile);
      g_editor.vim_mode = VM_INSERT;
      return;
    }
    case 'd': {
      g_editor.curFile.mkUndoMemento();
      fileConfigDeleteCurrentRow(&g_editor.curFile);
      return;
    }
    case '$': {
      fileConfigCursorMoveEndOfRow(&g_editor.curFile);
      return;
    }
    case '0': {
      fileConfigCursorMoveBeginOfRow(&g_editor.curFile);
      return;
    }

    case 'g':
    case ' ':
    case '?': {
      // TODO: make this more local.
      fileConfigRequestGoalState(&g_editor.curFile);
      g_editor.vim_mode = VM_INFOVIEW_DISPLAY_GOAL;
      return;
    }
    case 'i':
      g_editor.curFile.mkUndoMemento();
      g_editor.vim_mode = VM_INSERT;
      return;
    } // end switch over key.
  } // end mode == VM_NORMAL
  else {
    assert (g_editor.vim_mode == VM_INSERT); 

    // make an undo memento every second.
    g_editor.curFile.mkUndoMementoRecent();

    switch (c) { // behaviors only in edit mode.
    case CTRL_KEY('\\'): {
      g_editor.curFile.ctrlp.previous_state = VM_COMPLETION;
      g_editor.vim_mode = VM_COMPLETION;
      return;
    }
    case CTRL_KEY('p'): {
      g_editor.curFile.ctrlp.previous_state = VM_INSERT;
      g_editor.vim_mode = VM_CTRLP;
      return;
    }
    case '\r':
      fileConfigInsertEnterKey(&g_editor.curFile);
      return;
    case KEYEVENT_BACKSPACE: { // this is backspace, apparently
      fileConfigBackspace(&g_editor.curFile);
      return;
    }
    // when switching to normal mode, sync the lean state. 
    case CTRL_KEY('c'): { // escape key
      g_editor.vim_mode = VM_NORMAL;
      fileConfigSave(&g_editor.curFile);
      fileConfigSyncLeanState(&g_editor.curFile);
      return;
   }
    default:
      if (isprint(c)){
        fileConfigInsertCharBeforeCursor(&g_editor.curFile, c);
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
  const char *abbrev_path = get_abbreviations_dict_path();
  printf("[loading abbreviations.json from '%s']\n", abbrev_path);
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
char *get_executable_path() {
  const int BUFSIZE = 2048;
  char *buf = (char*)calloc(BUFSIZE, sizeof(char));
  const char *exe_path = "/proc/self/exe";
  int sz = readlink(exe_path, buf, BUFSIZE);

  if (sz == -1) {
    die("unable to get path of executable, failed to read '%s'.", exe_path);
  }
  return buf;
}

// get the path to `/path/to/exe/abbreviations.json`.
char *get_abbreviations_dict_path() {
  char *exe_folder = dirname(get_executable_path());
  const int BUFSIZE = 2048;
  char *buf = (char*)calloc(BUFSIZE, sizeof(char));
  snprintf(buf, BUFSIZE, "%s/%s", exe_folder, "abbreviations.json");  
  return buf;
}


// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_json(AbbreviationDict *dict, json_object *o) {
  assert(o && "illegal json file");
  assert(!dict->is_initialized);
  dict->nrecords = 0;

  json_object_object_foreach(o, key_, val_) {
    dict->nrecords++;
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
};

// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_file(AbbreviationDict *dict, const char *abbrev_path) {
  json_object *o = json_object_from_file(abbrev_path);
  if (o == NULL) {
    die("unable to load abbreviations from file '%s'.\n", abbrev_path);
  }
  load_abbreviation_dict_from_json(dict, o);
};

/** ctrlp **/


// convert level 'quitPressed' into edge trigger.
bool ctrlpWhenQuit(CtrlPView *view) {
  const bool out = view->quitPressed;
  view->quitPressed = false;
  return out;
}

void ctrlpHandleInput(CtrlPView *view, int c) {
  assert(view->textCol <= view->textArea.ncodepoints());
  if (view->textAreaMode == TAM_Normal) {
    if (c == 'h' || c == KEYEVENT_ARROW_LEFT) {
      view->textCol = view->textCol.sub0(1);
    } else if (c == 'l' || c == KEYEVENT_ARROW_RIGHT) {
      view->textCol = 
        clampu<Size<Codepoint>>(view->textCol + 1, view->textArea.ncodepoints());
    } else if (c == '$') {
      view->textCol = view->textArea.ncodepoints();
    } else if (c == '0') {
      view->textCol = 0;
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
    } else if (c == CTRL_KEY('c') || c == CTRL_KEY('p') || c == 'q') {
      // quit and go back to previous state.
      view->quitPressed = true;
    }
 
  } else {
    assert(view->textAreaMode == TAM_Insert);
    if (c == CTRL_KEY('c') || c == CTRL_KEY('p')) {
      view->textAreaMode = TAM_Normal;
      // quit and go back to previous state.
    } else if (c == KEYEVENT_ARROW_LEFT) {
      view->textCol = view->textCol.sub0(1);
    } else if (c == KEYEVENT_ARROW_RIGHT) {
      view->textCol = 
        clampu<Size<Codepoint>>(view->textCol + 1, view->textArea.ncodepoints());
    } else if (c == CTRL_KEY('p')) {
      view->quitPressed = true;
    } else if (c == KEYEVENT_BACKSPACE) {
      if (view->textArea.ncodepoints() > 0) {
        view->textArea.delCodepointAt(view->textCol.largestIx());
        view->textCol = view->textCol.sub0(1);
      }
    } else if (isprint(c)) {
      view->textArea.insertCodepointBefore(view->textCol, (const char *)&c);
      view->textCol += 1;
    }
  } 
}

void ctrlpDraw(const CtrlPView *view, abuf *ab) {
  // VT100 escapes.
  // \x1b: escape. J: erase in display. [2J: clear entire screen
  ab->appendstr("\x1b[2J");
  // H: cursor position. [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab->appendstr("\x1b[1;1H");

  ab->appendstr("\x1b[?25l"); // hide cursor

  // append format string.
  ab->appendfmtstr(120, "┎CTRLP MODE (%s)┓\r\n", 
    textAreaModeToString(view->textAreaMode));
  
  // need to consider a window around the character.
  // this is a nice design pattern.
  // TODO: refactor into SquishyAABB.
  const int LEFT_WINDOW_PADDING = 40;
  const int RIGHT_WINDOW_PADDING = 40;
  const int BORDER_WIDTH = 3; // ellipsis width;
  const int LEFT_WINDOW_MARGIN = LEFT_WINDOW_PADDING - BORDER_WIDTH;
  const int RIGHT_WINDOW_MARGIN = RIGHT_WINDOW_PADDING + BORDER_WIDTH;

  const int NCHARS = 40;

  Size<Codepoint> lm = view->textCol.sub0(LEFT_WINDOW_MARGIN);
  Size<Codepoint> lp = view->textCol.sub0(LEFT_WINDOW_PADDING);
  Size<Codepoint> rp = clampu(view->textCol + RIGHT_WINDOW_PADDING, view->textArea.ncodepoints());
  Size<Codepoint> rm = clampu(view->textCol + RIGHT_WINDOW_MARGIN, view->textArea.ncodepoints());

  assert(lp <= view->textCol);
  assert(view->textCol <= rp);

  // ab->appendstr(ESCAPE_CODE_DULL);
  for(auto i = lm; i < lp; ++i) {
    ab->appendChar('.');
  }
  // ab->appendstr(ESCAPE_CODE_UNSET);
  for(auto i = lp; i <= rp; ++i) {
    drawColWithCursor(ab, &view->textArea, i, view->textCol, view->textAreaMode);
  }
  // ab->appendstr(ESCAPE_CODE_DULL);
  for(auto i = rp+1; i < rm; ++i) {
    ab->appendChar('.');
  }
  // ab->appendstr(ESCAPE_CODE_UNSET);
  ab->appendstr("\r\n");

  // H: cursor position. [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab->appendstr("\x1b[1;1H");

}
