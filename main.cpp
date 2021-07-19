#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

const char *VERSION = "0.0.1";

// https://vt100.net/docs/vt100-ug/chapter3.html#CPR

// control key: maps to 1...26,
// 00011111
// #define CTRL_KEY(k) ((k) & ((1 << 6) - 1))
#define CTRL_KEY(k) ((k)&0x1f)

/*** data ***/

struct editorConfig {
  struct termios orig_termios;
  int screenrows;
  int screencols;
} E;

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

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
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

/*** append buffer ***/
struct abuf {
  char *b = nullptr;
  int len = 0;
  abuf() {}

  void appendbuf(const char *s, int slen) {
    this->b = (char *)realloc(b, len + slen);
    assert(this->b && "unable to append string");
    memcpy(this->b + len, s, slen);
    this->len += slen;
  }

  void appendstr(const char *s) { appendbuf(s, strlen(s)); }

  ~abuf() { free(b); }
};

/*** output ***/
void editorDrawRows(abuf &ab) {

  // When we print the nal tilde, we then print a
  // "\r\n" like on any other line, but this causes the terminal to scroll in
  // order to make room for a new, blank line. Let’s make the last line an
  // exception when we print our
  // "\r\n" ’s.

  for (int y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = sprintf(welcome, "Kilo editor -- version %s", VERSION);
      welcomelen = std::min<int>(welcomelen, E.screencols);

      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        ab.appendstr("~");
        padding--;
      }
      while (padding--) { ab.appendstr(" "); };


      ab.appendbuf(welcome, welcomelen);
    } else {
      ab.appendstr("~");
    }

    // The K command (Erase In Line) erases part of the current line.
    // by default, arg is 0, which erases everything to the right of the cursor.
    ab.appendstr("\x1b[K");
    if (y < E.screenrows - 1) {
      ab.appendstr("\r\n");
    }
  }
}

void editorRefreshScreen() {
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

  ab.appendstr("\x1b[H");

  // show hidden cursor
  ab.appendstr("\x1b[?25h");

  write(STDOUT_FILENO, ab.b, ab.len);
}

/*** input ***/
void editorProcessKeypress() {
  char c = editorReadKey();
  switch (c) {

  case CTRL_KEY('q'):
    exit(0);
    break;
  }
}

/*** init ***/

void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  };
}

int main() {
  enableRawMode();
  initEditor();
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  };

  return 0;
}
