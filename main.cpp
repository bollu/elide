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
#include "data_structures.h"


void  disableRawMode();

/*** lean server state ***/
// quick refresher on how pipes work:
// 1. pipe(fds) creates a pipe, where fds[0] is the read fd, fds[1] is the write fd.
//   one can only write to fds[1], and one can only read from fds[0].
// 2. dup2(x, y) ties the two file descriptors together, making a write to one equal to writing to the other.
// 2a. If we want to write into the STDIN of a process via the output of a pipe, we would use
//      `dup2(STDIN_FILENO, fds[PIPE_READ_IX])`;
// 3. for cleanliness, we close() the sides of the pipes that must be left unsused on each side.
//    so on the producer side `write(fds[1], data)`, we close the consumer fd `close(fds[0])` and vice versa.
void _exec_lean_server_on_child(LeanServerInitKind init_kind) {

  int process_error = 0;
  if (init_kind == LST_LEAN_SERVER) {
    fprintf(stderr, "starting 'lean --server'...\n");
    const char *process_name = "lean";
    char * const argv[] = { strdup(process_name), "--server", NULL };
    process_error = execvp(process_name, argv);
  } else if (init_kind == LST_LAKE_SERVE) {
    const char * process_name = "lake";
    char * const argv[] = { strdup(process_name), "serve", NULL };
    process_error = execvp(process_name, argv);
  } else {
    assert(false && "unhandled LeanServerInitKind.");
  }

  if (process_error == -1) {
    perror("failed to launch lean server");
    abort();
  }
}
// create a new lean server.
LeanServerState init_lean_server(LeanServerInitKind init_kind) {
  LeanServerState state;
  (void)pipe(state.parent_buffer_to_child_stdin);
  (void)pipe(state.child_stdout_to_parent_buffer);
  (void)pipe(state.child_stderr_to_parent_buffer);
  
  pid_t childpid = fork();
  if(childpid == -1) {
    perror("ERROR: fork failed.");
    exit(1);
  };

  if(childpid == 0) {
    disableRawMode(); // go back to normal mode of I/O.

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

    _exec_lean_server_on_child(init_kind);
  } else {
    // parent will only write into this pipe, so close the read end.
    close(state.parent_buffer_to_child_stdin[PIPE_READ_IX]);
    // parent will only read from this pipe so close the wite end.
    close(state.child_stdout_to_parent_buffer[PIPE_WRITE_IX]);
    // parent will only read from this pipe so close the wite end.
    close(state.child_stderr_to_parent_buffer[PIPE_WRITE_IX]);

    fprintf(stderr, "\nPARENT: sleeping for 3s\n");
    sleep(3);
    fprintf(stderr, "\nPARENT: writing data into buffer\n");
    const char *data = "{ vscode }\n";
    write(state.parent_buffer_to_child_stdin[PIPE_WRITE_IX], data, strlen(data));
    // flush(state.parent_buffer_to_child_stdin[PIPE_WRITE_IX]);
    fprintf(stderr, "\nPARENT: sleeping...\n");
    sleep(3);
    fprintf(stderr, "\nPARENT: reading from pipe...\n");
    static const int BUF_SIZE = 4096;
    char BUF[BUF_SIZE];
    int nread;

    nread = read(state.child_stdout_to_parent_buffer[PIPE_READ_IX], BUF, BUF_SIZE);
    BUF[nread] = 0;
    fprintf(stderr, "\nPARENT: child response (stdout): '%s'\n", BUF);
    sleep(1);

    nread = read(state.child_stderr_to_parent_buffer[PIPE_READ_IX], BUF, BUF_SIZE);
    BUF[nread] = 0;
    fprintf(stderr, "\nPARENT: child response (stderr): '%s'\n", BUF);
    sleep(1);

    fprintf(stderr, "\nPARENT: quitting...\n");
    exit(1);

  }
  // return lean server state to the parent process.
  return state;
};

// tactic mode goal.
void lean_server_get_tactic_mode_goal_state(LeanServerState state, LeanServerCursorInfo cinfo);
// term mode goal
void lean_server_get_term_mode_goal_state(LeanServerState state, LeanServerCursorInfo cinfo);
// autocomplete.
void lean_server_get_completion_at_point(LeanServerState state, LeanServerCursorInfo cinfo);



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
editorConfig E;

/*** terminal ***/

void die(const char *s) {
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  // explain errno before dying with mesasge s.
  perror(s);
  exit(1);
}

void disableRawMode() {

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcgetattr");
  };
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
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
  } else if (c == 'h' && E.file_mode == FM_VIEW) {
    return ARROW_LEFT;
  } else if (c == 'j' && E.file_mode == FM_VIEW) {
    return ARROW_DOWN;
  } else if (c == 'k' && E.file_mode == FM_VIEW) {
    return ARROW_UP;
  } else if (c == 'l' && E.file_mode == FM_VIEW) {
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

  printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);
}

int getWindowSize(int *rows, int *cols) {

  getCursorPosition(rows, cols);
  printf("\r\nrows: %d | cols: %d\r\n", *rows, *cols);

  struct winsize ws;
  // TIOCGWINSZ: terminal IOctl get window size.
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  }
  return 0;
}

/*** row operations ***/

// insert a new row at location `at`, and store `s` at that row.
// so rows'[at] = <new str>, rows'[at+k] = rows[at + k - 1];
// This also copies the indentation from the previous line into the new line.
void editorInsertRow(int at, const char *s, size_t len) {
  if (at < 0 || at > E.numrows) {
    return;
  }

  E.row = (erow *)realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  E.row[at].size = len;
  E.row[at].chars = (char *)malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].update(E);

  E.numrows++;
  E.dirty = true;
}

// functionality superceded by |editorInsertRow|
// void editorAppendRow(const char *s, size_t len) {
//   E.row = (erow *)realloc(E.row, sizeof(erow) * (E.numrows + 1));
//   const int at = E.numrows;
//   // TODO: placement-new.
//   E.row[at].size = len;
//   E.row[at].chars = (char *)malloc(len + 1);
//   memcpy(E.row[at].chars, s, len);
//   E.row[at].chars[len] = '\0';

//   E.row[at].rsize = 0;
//   E.row[at].render = NULL;
//   E.row[at].update();
//   E.numrows++;
//   E.dirty = true;
// }

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  E.dirty = true;
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
}

void editorRowDelChar(erow *row, int at) {
  E.dirty = true;
  assert(at >= 0);
  assert(at < row->size);
  if (at < 0 || at >= row->size) {
    return;
  }
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  row->update(E);
}


bool is_space_or_tab(char c) {
  return c == ' ' || c == '\t';
}

/*** editor operations ***/
void editorInsertNewline() {
  E.dirty = true;
  if (E.cx == 0) {
    // at first column, insert new row.
    editorInsertRow(E.cy, "", 0);
    // place cursor at next row (E.cy + 1), first column (cx=0)
    E.cy++;
    E.cx = 0;
  } else {
    // at column other than first, so chop row and insert new row.
    erow *row = &E.row[E.cy];
    // legal previous row, copy the indentation.
    // note that the checks 'num_indent < row.size' and 'num_indent < E.cx' are *not* redundant.
    // We only want to copy as much indent exists upto the cursor.
    int num_indent = 0;
    for (num_indent = 0;
   num_indent < row->size &&
   num_indent < E.cx && is_space_or_tab(row->chars[num_indent]);
   num_indent++) {};
    char *new_row_contents = (char *)malloc(sizeof(char)*(row->size - E.cx + num_indent));
    for(int i = 0; i < num_indent; ++i) {
      new_row_contents[i] = row->chars[i]; // copy the spaces over.
    }
    for(int i = E.cx; i < row->size; ++i) {
      new_row_contents[num_indent + (i - E.cx)] = row->chars[i];
    }
    // create a row at E.cy + 1 containing data row[E.cx:...]
    editorInsertRow(E.cy + 1, new_row_contents, row->size - E.cx + num_indent);

    // pointer invalidated, get pointer to current row again,
    row = &E.row[E.cy];
    // chop off at row[...:E.cx]
    row->size = E.cx;
    row->chars[row->size] = '\0';
    row->update(E);

    // place cursor at next row (E.cy + 1), column of the indent.
    E.cy++;
    E.cx = num_indent;
  }
}

void editorInsertChar(int c) {
  E.dirty = true;
  if (E.cy == E.numrows) {
    // editorAppendRow("", 0);
    editorInsertRow(E.numrows, "", 0);
  }
  E.row[E.cy].insertChar(E.cx, c, E);
  E.cx++;
}

void editorDelChar() {
  E.dirty = true;
  if (E.cy == E.numrows) {
    return;
  }
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    // place cursor at last column of prev row.
    E.cx = E.row[E.cy - 1].size;
    // append string.
    E.row[E.cy - 1].appendString(row->chars, row->size, E);
    // delete current row
    editorDelRow(E.cy);
    // go to previous row.
    E.cy--;
  }
}

/*** file i/o ***/
void editorOpen(const char *filename) {
  free(E.filepath);
  E.filepath = strdup(filename);

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
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
}

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  for (int j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;
  char *buf = (char *)malloc(totlen);
  char *p = buf;
  for (int j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}


void editorSave() {
  if (E.filepath == NULL || !E.dirty) {
    return;
  }
  int len;
  char *buf = editorRowsToString(&len);
  // | open for read and write
  // | create if does not exist
  // 0644: +r, +w
  int fd = open(E.filepath, O_RDWR | O_CREAT, 0644);
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
  E.dirty = false;
  close(fd);
  free(buf);
}

/** find **/

void editorFind() {
  char *query = editorPrompt("Search: %s (ESC to cancel)");
  if (query == NULL) return;
  int i;
  for (i = 0; i < E.numrows; i++) {
    erow *row = &E.row[i];
    char *match = strstr(row->render, query);
    if (match) {
      E.cy = i;
      E.cx = match - row->render;
      E.rowoff = E.numrows;
      break;
    }
  }
  free(query);
}


/*** append buffer ***/

/*** output ***/

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = E.row[E.cy].cxToRx(E.cx);
  }
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
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

  // When we print the nal tilde, we then print a
  // "\r\n" like on any other line, but this causes the terminal to scroll in
  // order to make room for a new, blank line. Let’s make the last line an
  // exception when we print our
  // "\r\n" ’s.

  // plus one at the end for the pipe, and +1 on the num_digits so we start from '1'.
  const int LINE_NUMBER_NUM_CHARS = num_digits(E.screenrows + E.rowoff + 1) + 1;
  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    // convert the line number into a string, and write it.
    {
      char *line_number_str = (char *)calloc(sizeof(char), (LINE_NUMBER_NUM_CHARS + 1)); // TODO: allocate once.
      int ix = write_int_to_str(line_number_str, filerow + 1);
      while(ix < LINE_NUMBER_NUM_CHARS - 1) {
        line_number_str[ix] = ' ';
        ix++;
      }
      line_number_str[ix] = '|';
      ab.appendstr(line_number_str);
      free(line_number_str);
    }

    // code in view mode is renderered gray
    if (E.file_mode == FM_VIEW) {
      ab.appendstr("\x1b[90;40m"); // gray on black
    }

    if (filerow < E.numrows) {
      int len = clamp(0, E.row[filerow].rsize - E.coloff, E.screencols - LINE_NUMBER_NUM_CHARS);

      // int len = E.row[filerow].size;
      // if (len > E.screencols)
      //   len = E.screencols;
      ab.appendbuf(E.row[filerow].render + E.coloff, len);

    } else {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = sprintf(welcome, "Kilo editor -- version %s", VERSION);
        welcomelen = std::min<int>(welcomelen, E.screencols);

        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          ab.appendstr("~");
          padding--;
        }
        while (padding--) {
          ab.appendstr(" ");
        };

        ab.appendbuf(welcome, welcomelen);
      } else {
        ab.appendstr("~");
      }
    }

    // The K command (Erase In Line) erases part of the current line.
    // by default, arg is 0, which erases everything to the right of the
    // cursor.
    ab.appendstr("\x1b[K");

    // always append a space, since we decrement a row from screen rows
    // to make space for status bar.
    // if (y < E.screenrows - 1) {
    ab.appendstr("\r\n");
    
    // code in view mode is renderered gray
    if (E.file_mode == FM_VIEW) {
      ab.appendstr("\x1b[0m"); // reset.
    }
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
                     E.filepath ? E.filepath : "[No Name]", E.numrows);

  len = std::min<int>(len, E.screencols);
  ab.appendstr(status);

  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
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
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) {
    msglen = E.screencols;
  }

  E.statusmsg[msglen] = '\0';

  if (msglen && time(NULL) - E.statusmsg_time < 5) {
    ab.appendstr(E.statusmsg);
  }
}

void editorRefreshScreen() {

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
  // ab.appendstr("\x1b[2J");

  // H: cursor position
  // [<row>;<col>H   (args separated by ;).
  // Default arguments for H is 1, so it's as if we had sent [1;1H
  ab.appendstr("\x1b[H");

  editorDrawRows(ab);
  editorDrawStatusBar(ab);
  editorDrawMessageBar(ab);

  // move cursor to correct row;col.
  char buf[32];
  const int LINE_NUMBER_NUM_CHARS = num_digits(E.screenrows + E.rowoff + 1) + 1;
  sprintf(buf, "\x1b[%d;%dH", E.cy - E.rowoff + 1, E.rx - E.coloff + 1 + LINE_NUMBER_NUM_CHARS);
  ab.appendstr(buf);
  // ab.appendstr("\x1b[H"); < now place cursor at right location!

  // show hidden cursor
  ab.appendstr("\x1b[?25h");

  write(STDOUT_FILENO, ab.b, ab.len);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    } else if (E.cy > 0) {
      // move back line.
      assert(E.cx == 0);
      E.cy--;
      E.cx = E.row[E.cy].size;
    }

    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size) {
      E.cx++;
    } else if (row && E.cx == row->size) {
      assert(E.cx == row->size);
      E.cy++;
      E.cx = 0;
    }

    break;
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows) {
      E.cy++;
    }
    break;
  case PAGE_DOWN:
    E.cy = std::min<int>(E.cy + E.screenrows / 4, E.numrows);
    break;
  case PAGE_UP:
    E.cy = std::max<int>(E.cy - E.screenrows / 4, 0);
    break;
  }

  // snap to next line.
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  const int c = editorReadKey();

  // behaviours common to both modes
  switch (c) {
  case CTRL_KEY('q'): {
    editorSave();
    die("bye!");
    return;
  }
  case CTRL_KEY('f'):
    editorFind();
    return;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_RIGHT:
  case ARROW_LEFT:
  case PAGE_DOWN:
  case PAGE_UP:
    editorMoveCursor(c);
    return;
  }

  if (E.file_mode == FM_VIEW) { // behaviours only in  view mode
    switch (c) {
    case 'i':
      E.file_mode = FM_EDIT; return;
    } // end switch over key.
  } // end mode == FM_VIEW
  else {
    assert (E.file_mode == FM_EDIT); 
    switch (c) { // behaviors only in edit mode.
    case '\r':
      editorInsertNewline();
      return;
    case CTRL_KEY('c'): {
      E.file_mode = FM_VIEW;
      return;
    }
    case DEL_CHAR: {
      editorDelChar();
      return;
    }
    case '\x1b': {// escape key
      E.file_mode = FM_VIEW;
      return;
   }
    default:
      editorInsertChar(c);
      return;
    } // end switch case.
  } // end mode == FM_EDIT
}

char *editorPrompt(const char *prompt) {
  size_t bufsize = 128;
  char *buf = (char *)malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
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
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  };
  static const int BOTTOM_INFO_PANE_HEIGHT = 2;
  E.screenrows -= BOTTOM_INFO_PANE_HEIGHT;
}

int main(int argc, char **argv){
  enableRawMode();
  initEditor();

  
  E.lean_sever_state = init_lean_server(LST_LEAN_SERVER); // start lean --server.  

  editorSetStatusMessage("HELP: Ctrl-Q = quit");

  char *filepath = NULL;
  if (argc >= 2) {
    filepath = argv[1];
  } else {
    filepath = strdup("/tmp/edtr-scratch");
  }
  editorOpen(filepath);

  while (1) {
    editorSave();
    editorRefreshScreen();
    editorProcessKeypress();
  };

  return 0;
}
