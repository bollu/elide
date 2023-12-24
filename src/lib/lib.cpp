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

#define CHECK_POSIX_CALL_0(x) { do { int success = x == 0; if(!success) { perror("POSIX call failed"); }; assert(success); } while(0); }
// check that minus 1 is notreturned.
#define CHECK_POSIX_CALL_M1(x) { do { int fail = x == -1; if(fail) { perror("POSIX call failed"); }; assert(!fail); } while(0); }


void disableRawMode();
void enableRawMode();


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

  char *lakefile_dirpath = get_lakefile_dirpath(file_path);
  fprintf(stderr, "lakefile_dirpath: '%s'\n", lakefile_dirpath);
  free(lakefile_dirpath);

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
    char *lakefile_dirpath = get_lakefile_dirpath(file_path);
    _exec_lean_server_on_child(lakefile_dirpath);

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
  int content_length = atoi(child_stdout_buffer.b + content_length_begin_ix + strlen(CONTENT_LENGTH_STR));

  // we don't have enough data in the buffer to read the content length
  const int header_line_end_ix = header_line_begin_ix + strlen(DOUBLE_NEWLINE_STR);
  if (child_stdout_buffer.len < header_line_end_ix + content_length) {
    return NULL;
  }

  // parse.
  json_tokener *tok = json_tokener_new(); // TODO: do not create/destroy this object each time.
  json_object *o = json_tokener_parse_ex(tok,
    child_stdout_buffer.b + header_line_end_ix, content_length);
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
char *editorPrompt(const char *prompt);

// https://vt100.net/docs/vt100-ug/chapter3.html#CPR

// control key: maps to 1...26,
// 00011111
// #define CTRL_KEY(k) ((k) & ((1 << 6) - 1))
#define CTRL_KEY(k) ((k)&0x1f)

int clamp(int lo, int val, int hi) {
  return std::min<int>(std::max<int>(lo, val), hi);
}

/*** data ***/
EditorConfig g_editor;

/*** terminal ***/

void die(const char *s) {
  // clear screen
  // CHECK_POSIX_CALL_0(write(STDOUT_FILENO, "\x1b[2J", 4));
  // CHECK_POSIX_CALL_0(write(STDOUT_FILENO, "\x1b[H", 3));
  // explain errno before dying with mesasge s.
  perror(s);
  exit(1);
}

void disableRawMode() {

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

int editorReadKey() {
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
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      }
      // translate arrow keys to vim :)
      switch (seq[1]) {
      case 'A':
        return ARROW_UP;
      case 'B':
        return ARROW_DOWN;
      case 'C':
        return ARROW_RIGHT;
      case 'D':
        return ARROW_LEFT;
      }
    };

    return '\x1b';
  } 
  else if (c == CTRL_KEY('d')) {
    return PAGE_DOWN;
  } else if (c == CTRL('u')) {
    return PAGE_UP;
  } else if (c == 'h' && g_editor.vim_mode == VM_NORMAL) {
    return ARROW_LEFT;
  } else if (c == 'j' && g_editor.vim_mode == VM_NORMAL) {
    return ARROW_DOWN;
  } else if (c == 'k' && g_editor.vim_mode == VM_NORMAL) {
    return ARROW_UP;
  } else if (c == 'l' && g_editor.vim_mode == VM_NORMAL) {
    return ARROW_RIGHT;
  } else if (c == 127) {
    return DEL_CHAR;
  } else {
    return c;
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
void editorInsertRow(int at, const char *s, size_t len) {
  if (at < 0 || at > g_editor.curFile.numrows) {
    return;
  }

  g_editor.curFile.row = (FileRow *)realloc(g_editor.curFile.row, sizeof(FileRow) * (g_editor.curFile.numrows + 1));
  memmove(&g_editor.curFile.row[at + 1], &g_editor.curFile.row[at], sizeof(FileRow) * (g_editor.curFile.numrows - at));
  g_editor.curFile.row[at].size = len;
  g_editor.curFile.row[at].chars = (char *)malloc(len + 1);
  memcpy(g_editor.curFile.row[at].chars, s, len);
  g_editor.curFile.row[at].chars[len] = '\0';
  g_editor.curFile.row[at].rsize = 0;
  g_editor.curFile.row[at].render = NULL;
  g_editor.curFile.row[at].update(g_editor.curFile);

  g_editor.curFile.numrows++;
  g_editor.curFile.makeDirty();
}

// functionality superceded by |editorInsertRow|
// void editorAppendRow(const char *s, size_t len) {
//   g_editor.curFile.row = (FileRow *)realloc(g_editor.curFile.row, sizeof(FileRow) * (g_editor.curFile.numrows + 1));
//   const int at = g_editor.curFile.numrows;
//   // TODO: placement-new.
//   g_editor.curFile.row[at].size = len;
//   g_editor.curFile.row[at].chars = (char *)malloc(len + 1);
//   memcpy(g_editor.curFile.row[at].chars, s, len);
//   g_editor.curFile.row[at].chars[len] = '\0';

//   g_editor.curFile.row[at].rsize = 0;
//   g_editor.curFile.row[at].render = NULL;
//   g_editor.curFile.row[at].update();
//   g_editor.curFile.numrows++;
//   g_editor.curFile.is_dirty = true;
// }

void editorFreeRow(FileRow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  g_editor.curFile.makeDirty();
  if (at < 0 || at >= g_editor.curFile.numrows)
    return;
  editorFreeRow(&g_editor.curFile.row[at]);
  memmove(&g_editor.curFile.row[at], &g_editor.curFile.row[at + 1], sizeof(FileRow) * (g_editor.curFile.numrows - at - 1));
  g_editor.curFile.numrows--;
}

void editorRowDelChar(FileRow *row, int at) {
  g_editor.curFile.makeDirty();
  assert(at >= 0);
  assert(at < row->size);
  if (at < 0 || at >= row->size) {
    return;
  }
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  row->update(g_editor.curFile);
}


bool is_space_or_tab(char c) {
  return c == ' ' || c == '\t';
}

/*** editor operations ***/
void editorInsertNewline() {
  g_editor.curFile.makeDirty();
  if (g_editor.curFile.cursor.x == 0) {
    // at first column, insert new row.
    editorInsertRow(g_editor.curFile.cursor.y, "", 0);
    // place cursor at next row (g_editor.curFile.cursor.y + 1), first column (cx=0)
    g_editor.curFile.cursor.y++;
    g_editor.curFile.cursor.x = 0;
  } else {
    // at column other than first, so chop row and insert new row.
    FileRow *row = &g_editor.curFile.row[g_editor.curFile.cursor.y];
    // legal previous row, copy the indentation.
    // note that the checks 'num_indent < row.size' and 'num_indent < g_editor.curFile.cursor.x' are *not* redundant.
    // We only want to copy as much indent exists upto the cursor.
    int num_indent = 0;
    for (num_indent = 0;
   num_indent < row->size &&
   num_indent < g_editor.curFile.cursor.x && is_space_or_tab(row->chars[num_indent]);
   num_indent++) {};
    char *new_row_contents = (char *)malloc(sizeof(char)*(row->size - g_editor.curFile.cursor.x + num_indent));
    for(int i = 0; i < num_indent; ++i) {
      new_row_contents[i] = row->chars[i]; // copy the spaces over.
    }
    for(int i = g_editor.curFile.cursor.x; i < row->size; ++i) {
      new_row_contents[num_indent + (i - g_editor.curFile.cursor.x)] = row->chars[i];
    }
    // create a row at g_editor.curFile.cursor.y + 1 containing data row[g_editor.curFile.cursor.x:...]
    editorInsertRow(g_editor.curFile.cursor.y + 1, new_row_contents, row->size - g_editor.curFile.cursor.x + num_indent);

    // pointer invalidated, get pointer to current row again,
    row = &g_editor.curFile.row[g_editor.curFile.cursor.y];
    // chop off at row[...:g_editor.curFile.cursor.x]
    row->size = g_editor.curFile.cursor.x;
    row->chars[row->size] = '\0';
    row->update(g_editor.curFile);

    // place cursor at next row (g_editor.curFile.cursor.y + 1), column of the indent.
    g_editor.curFile.cursor.y++;
    g_editor.curFile.cursor.x = num_indent;
  }
}

void editorInsertChar(int c) {
  g_editor.curFile.makeDirty();
  if (g_editor.curFile.cursor.y == g_editor.curFile.numrows) {
    // editorAppendRow("", 0);
    editorInsertRow(g_editor.curFile.numrows, "", 0);
  }
  g_editor.curFile.row[g_editor.curFile.cursor.y].insertChar(g_editor.curFile.cursor.x, c, g_editor.curFile);
  g_editor.curFile.cursor.x++;
}

void editorDelChar() {
  g_editor.curFile.makeDirty();
  if (g_editor.curFile.cursor.y == g_editor.curFile.numrows) {
    return;
  }
  if (g_editor.curFile.cursor.x == 0 && g_editor.curFile.cursor.y == 0)
    return;

  FileRow *row = &g_editor.curFile.row[g_editor.curFile.cursor.y];
  if (g_editor.curFile.cursor.x > 0) {
    editorRowDelChar(row, g_editor.curFile.cursor.x - 1);
    g_editor.curFile.cursor.x--;
  } else {
    // place cursor at last column of prev row.
    g_editor.curFile.cursor.x = g_editor.curFile.row[g_editor.curFile.cursor.y - 1].size;
    // append string.
    g_editor.curFile.row[g_editor.curFile.cursor.y - 1].appendString(row->chars, row->size, g_editor.curFile);
    // delete current row
    editorDelRow(g_editor.curFile.cursor.y);
    // go to previous row.
    g_editor.curFile.cursor.y--;
  }
}


void fileConfigLaunchLeanServer(FileConfig *file_config) {


  assert(file_config->absolute_filepath != NULL);
  assert(file_config->lean_server_state.initialized == false);
  file_config->lean_server_state = LeanServerState::init(file_config->absolute_filepath); // start lean --server.  
  // file_config->lean_server_state = LeanServerState::init(NULL); // start lean --server.  

  json_object *req = lspCreateInitializeRequest();
  LspRequestId request_id = file_config->lean_server_state.write_request_to_child_blocking("initialize", req);

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
    int len;
    file_config->text_document_item.text = editorRowsToString(&len);
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
    // TODO: decrease refcount.
    file_config->leanInfoViewPlainGoal = nullptr;
  }

  assert(file_config->leanInfoViewPlainGoal == nullptr);


  req = lspCreateLeanPlainGoalRequest(file_config->text_document_item.uri, 
    Position(file_config->cursor.y, file_config->cursor.x));
  request_id = file_config->lean_server_state.write_request_to_child_blocking("$/lean/plainGoal", req);
  file_config->leanInfoViewPlainGoal = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

  // $/lean/plainTermGoal
  if (file_config->leanInfoViewPlainTermGoal) {
    // TODO: decrease refcount.
    file_config->leanInfoViewPlainTermGoal = nullptr;
  }

  assert(file_config->leanInfoViewPlainTermGoal == nullptr);
  req = lspCreateLeanPlainTermGoalRequest(file_config->text_document_item.uri, 
    Position(file_config->cursor.y, file_config->cursor.x));
  request_id = file_config->lean_server_state.write_request_to_child_blocking("$/lean/plainTermGoal", req);
  file_config->leanInfoViewPlainTermGoal = file_config->lean_server_state.read_json_response_from_child_blocking(request_id);

}


/*** file i/o ***/
void editorOpen(const char *filename) {
  free(g_editor.curFile.absolute_filepath);
  g_editor.curFile.absolute_filepath = strdup(filename);

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
    // editorAppendRow(line, linelen);
    editorInsertRow(g_editor.curFile.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  g_editor.curFile.is_initialized = true;
}


char *editorRowsToString(int *buflen) {
  int totlen = 0;
  for (int j = 0; j < g_editor.curFile.numrows; j++) {
    totlen += g_editor.curFile.row[j].size + 1;
  }
  *buflen = totlen;
  char *buf = (char *)malloc(totlen);
  char *p = buf;
  for (int j = 0; j < g_editor.curFile.numrows; j++) {
    memcpy(p, g_editor.curFile.row[j].chars, g_editor.curFile.row[j].size);
    p += g_editor.curFile.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}


void editorSave() {
  if (g_editor.curFile.absolute_filepath == NULL || !g_editor.curFile.is_dirty) {
    return;
  }
  int len;
  char *buf = editorRowsToString(&len);
  // | open for read and write
  // | create if does not exist
  // 0644: +r, +w
  int fd = open(g_editor.curFile.absolute_filepath, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
  }
  assert(fd != -1 && "unable to open file");
  // | set file to len.
  int err = ftruncate(fd, len);
  assert(err != -1 && "unable to truncate");
  int nwritten = write(fd, buf, len);
  assert(nwritten == len && "wasn't able to write enough bytes");
  editorSetStatusMessage("Saved file");
  g_editor.curFile.is_dirty = false;
  close(fd);
  free(buf);
}

/** find **/

void editorFind() {
  char *query = editorPrompt("Search: %s (ESC to cancel)");
  if (query == NULL) return;
  int i;
  for (i = 0; i < g_editor.curFile.numrows; i++) {
    FileRow *row = &g_editor.curFile.row[i];
    char *match = strstr(row->render, query);
    if (match) {
      g_editor.curFile.cursor.y = i;
      g_editor.curFile.cursor.x = match - row->render;
      g_editor.curFile.scroll_row_offset = g_editor.curFile.numrows;
      break;
    }
  }
  free(query);
}


/*** append buffer ***/

/*** output ***/

void editorScroll() {
  g_editor.curFile.rx = 0;
  assert (g_editor.curFile.cursor.y >= 0 && g_editor.curFile.cursor.y <= g_editor.curFile.numrows);
  if (g_editor.curFile.cursor.y < g_editor.curFile.numrows) {
    g_editor.curFile.rx = g_editor.curFile.row[g_editor.curFile.cursor.y].cxToRx(g_editor.curFile.cursor.x);
  }
  if (g_editor.curFile.cursor.y < g_editor.curFile.scroll_row_offset) {
    g_editor.curFile.scroll_row_offset = g_editor.curFile.cursor.y;
  }
  if (g_editor.curFile.cursor.y >= g_editor.curFile.scroll_row_offset + g_editor.screenrows) {
    g_editor.curFile.scroll_row_offset = g_editor.curFile.cursor.y - g_editor.screenrows + 1;
  }
  if (g_editor.curFile.rx < g_editor.curFile.scroll_col_offset) {
    g_editor.curFile.scroll_col_offset = g_editor.curFile.rx;
  }
  if (g_editor.curFile.rx >= g_editor.curFile.scroll_col_offset + g_editor.screencols) {
    g_editor.curFile.scroll_col_offset = g_editor.curFile.rx - g_editor.screencols + 1;
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
  // "\r\n" ’s.


  // plus one at the end for the pipe, and +1 on the num_digits so we start from '1'.
  const int LINE_NUMBER_NUM_CHARS = num_digits(g_editor.screenrows + g_editor.curFile.scroll_row_offset + 1) + 1;
  for (int y = 0; y < g_editor.screenrows; y++) {
    int filerow = y + g_editor.curFile.scroll_row_offset;

    // convert the line number into a string, and write it.
    {
      // code in view mode is renderered gray
      if (g_editor.vim_mode == VM_NORMAL) { ab.appendstr("\x1b[90;40m"); }

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

    if (filerow < g_editor.curFile.numrows) {
      int len = clamp(0, g_editor.curFile.row[filerow].rsize - g_editor.curFile.scroll_col_offset, g_editor.screencols - LINE_NUMBER_NUM_CHARS);
      // int len = g_editor.curFile.row[filerow].size;
      // if (len > g_editor.screencols)
      //   len = g_editor.screencols;
      ab.appendbuf(g_editor.curFile.row[filerow].render + g_editor.curFile.scroll_col_offset, len);

    } else {
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

void editorDrawStatusBar(abuf &ab) {
  // m: select graphic rendition
  // 1: bold
  // 4: underscore
  // 5: bliks
  // 7: inverted colors
  // can select all with [1;4;5;7m
  // 0: clear, default arg.
  ab.appendstr("\x1b[7m");

  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - 5%d lines",
                     g_editor.curFile.absolute_filepath ? g_editor.curFile.absolute_filepath : "[No Name]", g_editor.curFile.numrows);

  len = std::min<int>(len, g_editor.screencols);
  ab.appendstr(status);

  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", g_editor.curFile.cursor.y + 1, g_editor.curFile.numrows);

  while (len < g_editor.screencols) {
    if (g_editor.screencols - len == rlen) {
      ab.appendstr(rstatus);
      break;
    } else {
      ab.appendstr(" ");
      len++;
    }
  }
  ab.appendstr("\x1b[m");
  ab.appendstr("\r\n");
}

void editorDrawMessageBar(abuf &ab) {
  // [K: clear sequence
  ab.appendstr("\x1b[K");
  int msglen = strlen(g_editor.statusmsg);
  if (msglen > g_editor.screencols) {
    msglen = g_editor.screencols;
  }

  g_editor.statusmsg[msglen] = '\0';

  if (msglen && time(NULL) - g_editor.statusmsg_time < 5) {
    ab.appendstr(g_editor.statusmsg);
  }
}

void editorDrawNormalInsertMode() {

  initEditor();
  editorScroll();
  abuf ab;

  // It’s possible that the cursor might be displayed in the middle of the
  // screen somewhere for a split second while the terminal is drawing to the
  // screen. To make sure that doesn’t happen, let’s hide the cursor before
  // refreshing the screen, and show it again immediately after the refresh
  // finishes.
  ab.appendstr("\x1b[?25l"); // hide cursor

  // EDIT: no need to refresh screen, screen is cleared
  // line by line @ editorDrawRows.
  //
  // EDIT: I am not sure if this extra complexity is worth it!
  //
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
  // editorDrawStatusBar(ab);
  // editorDrawMessageBar(ab);

  // move cursor to correct row;col.
  char buf[32];
  const int LINE_NUMBER_NUM_CHARS = num_digits(g_editor.screenrows + g_editor.curFile.scroll_row_offset + 1) + 1;
  sprintf(buf, "\x1b[%d;%dH", g_editor.curFile.cursor.y - g_editor.curFile.scroll_row_offset + 1, g_editor.curFile.rx - g_editor.curFile.scroll_col_offset + 1 + LINE_NUMBER_NUM_CHARS);
  ab.appendstr(buf);

  // show hidden cursor
  ab.appendstr("\x1b[?25h");

  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.b, ab.len));
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
void editorDrawInfoView() {
  abuf ab;

  // It’s possible that the cursor might be displayed in the middle of the
  // screen somewhere for a split second while the terminal is drawing to the
  // screen. To make sure that doesn’t happen, let’s hide the cursor before
  // refreshing the screen, and show it again immediately after the refresh
  // finishes.
  ab.appendstr("\x1b[?25l"); // hide cursor

  // EDIT: no need to refresh screen, screen is cleared
  // line by line @ editorDrawRows.
  //
  // EDIT: I am not sure if this extra complexity is worth it!
  //
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

  // always append a space, since we decrement a row from screen rows
  // to make space for status bar.
  ab.appendstr("@@@ LEAN INFOVIEW @@@ \r\n");
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
      ab.appendstr("## Expected Type: --- \x1b[K \r\n");
    } else {
      assert(json_object_get_type(result) == json_type_object);
      json_object *result_goal = nullptr;
      json_object_object_get_ex(result, "goal", &result_goal);
      assert(result_goal != nullptr);
      assert(json_object_get_type(result_goal) == json_type_string);

      ab.appendstr("## Expected Type: \x1b[K \r\n");
      editorDrawInfoViewGoal(&ab, json_object_get_string(result_goal));

    }
  } while(0); // end info view plain term goal.

  do {
    json_object  *result = nullptr;
    json_object_object_get_ex(g_editor.curFile.leanInfoViewPlainGoal, "result", &result);
    if (result == nullptr) {
      ab.appendstr("## Tactic State: --- \x1b[K \r\n");
    } else {
      json_object *result_goals = nullptr;
      json_object_object_get_ex(result, "goals", &result_goals);
      assert(result_goals != nullptr);

      assert(json_object_get_type(result_goals) == json_type_array);
      if (json_object_array_length(result_goals) == 0) {
        ab.appendstr("## Tactic State: In tactic mode with no open tactic goal. \x1b[K \r\n");
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


  // move cursor to correct row;col.
  char buf[32];
  // H: cursor position
  // [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  sprintf(buf, "\x1b[%d;%dH", 1, 1);
  ab.appendstr(buf);

  // show hidden cursor
  ab.appendstr("\x1b[?25h");


  CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.b, ab.len));
}


void editorDraw() {
  if (g_editor.vim_mode == VM_NORMAL || g_editor.vim_mode == VM_INSERT) {
    editorDrawNormalInsertMode();
    return;
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

void editorMoveCursor(int key) {
  FileRow *row = (g_editor.curFile.cursor.y >= g_editor.curFile.numrows) ? NULL : &g_editor.curFile.row[g_editor.curFile.cursor.y];

  switch (key) {
  case ARROW_LEFT:
    if (g_editor.curFile.cursor.x != 0) {
      g_editor.curFile.cursor.x--;
    } else if (g_editor.curFile.cursor.y > 0) {
      // move back line.
      assert(g_editor.curFile.cursor.x == 0);
      g_editor.curFile.cursor.y--;
      g_editor.curFile.cursor.x = g_editor.curFile.row[g_editor.curFile.cursor.y].size;
    }

    break;
  case ARROW_RIGHT:
    if (row && g_editor.curFile.cursor.x < row->size) {
      g_editor.curFile.cursor.x++;
    } else if (row && g_editor.curFile.cursor.x == row->size) {
      assert(g_editor.curFile.cursor.x == row->size);
      g_editor.curFile.cursor.y++;
      g_editor.curFile.cursor.x = 0;
    }

    break;
  case ARROW_UP:
    if (g_editor.curFile.cursor.y > 0) {
      g_editor.curFile.cursor.y--;
    }
    break;
  case ARROW_DOWN:
    if (g_editor.curFile.cursor.y < g_editor.curFile.numrows) {
      g_editor.curFile.cursor.y++;
    }
    break;
  case PAGE_DOWN:
    g_editor.curFile.cursor.y = std::min<int>(g_editor.curFile.cursor.y + g_editor.screenrows / 4, g_editor.curFile.numrows);
    break;
  case PAGE_UP:
    g_editor.curFile.cursor.y = std::max<int>(g_editor.curFile.cursor.y - g_editor.screenrows / 4, 0);
    break;
  }

  // snap to next line.
  // row = (g_editor.curFile.cursor.y >= g_editor.curFile.numrows) ? NULL : &g_editor.curFile.row[g_editor.curFile.cursor.y];
  // int rowlen = row ? row->size : 0;
  // if (g_editor.curFile.cursor.x > rowlen) {
  //   g_editor.curFile.cursor.x = rowlen;
  // }
}

void editorProcessKeypress() {
  const int c = editorReadKey();

  // behaviours common to all modes
  switch (c) {
  case 'q':
  case CTRL_KEY('q'): {
    editorSave();
    die("bye!");
    return;
  }
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_RIGHT:
  case ARROW_LEFT:
  case PAGE_DOWN:
  case PAGE_UP:
    editorMoveCursor(c);
    return;
  }

  if (g_editor.vim_mode == VM_INFOVIEW_DISPLAY_GOAL) { // behaviours only in infoview mode
    switch (c) {
    case 'g':
    case '?':
      g_editor.vim_mode = VM_NORMAL; return;
    default:
      return;
    }
  }

  if (g_editor.vim_mode == VM_NORMAL) { // behaviours only in  view mode
    switch (c) {
    case 'g':
    case '?': {
      // TODO: make this more local.
      fileConfigRequestGoalState(&g_editor.curFile);
      g_editor.vim_mode = VM_INFOVIEW_DISPLAY_GOAL; return;
    }
    case 'i':
      g_editor.vim_mode = VM_INSERT; return;
    } // end switch over key.
  } // end mode == VM_NORMAL
  else {
    assert (g_editor.vim_mode == VM_INSERT); 
    switch (c) { // behaviors only in edit mode.
    case '\r':
      editorInsertNewline();
      return;
    case DEL_CHAR: {
      editorDelChar();
      return;
    }
    // when switching to normal mode, sync the lean state. 
    case CTRL_KEY('c'):
    case '\x1b':  { // escape key
      g_editor.vim_mode = VM_NORMAL;
      editorSave();
      fileConfigSyncLeanState(&g_editor.curFile);
      return;
   }
    default:
      editorInsertChar(c);
      return;
    } // end switch case.
  } // end mode == VM_INSERT
}

char *editorPrompt(const char *prompt) {
  size_t bufsize = 128;
  char *buf = (char *)malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorDraw();
    int c = editorReadKey();
    if (c == CTRL('g') || c == '\x1b') {
        editorSetStatusMessage(""); free(buf); return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = (char *)realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

/*** init ***/

void initEditor() {
  if (getWindowSize(&g_editor.screenrows, &g_editor.screencols) == -1) {
    die("getWindowSize");
  };
  static const int BOTTOM_INFO_PANE_HEIGHT = 2; g_editor.screenrows -= BOTTOM_INFO_PANE_HEIGHT;
}
