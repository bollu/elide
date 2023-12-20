#pragma once
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
#include "lib.h"

struct LeanServerCursorInfo {
  const char *file_path;
  int row;
  int col;
};


#define PIPE_WRITE_IX 1
#define PIPE_READ_IX 0

enum LeanServerInitKind {
    LST_LEAN_SERVER, // lean --servver
    LST_LAKE_SERVE, // lake serve
}; 

// https://tldp.org/LDP/lpg/node11.html
struct LeanServerState {
  int parent_buffer_to_child_stdin[2];
  int child_stdout_to_parent_buffer[2];
  int child_stderr_to_parent_buffer[2];
  int stdout_log_file; // file handle of stdout logging
  int stderr_log_file; // file handle of stderr logging
  pid_t childpid;

  void write_to_child(const char *buf, int len) const;
  int read_stdout_from_child(const char *buf, int bufsize) const;
  int read_stderr_from_child(const char *buf, int bufsize) const;
  static LeanServerState init(LeanServerInitKind init_kind);
};


static const int NSPACES_PER_TAB = 2;
static const char *VERSION = "0.0.1";


enum FileMode {
  FM_VIEW, // mode where code is only viewed and locked for editing.
  FM_EDIT, // mode where code is edited.
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


struct erow;

struct editorConfig {
  LeanServerState lean_sever_state;
  FileMode file_mode = FM_VIEW;
  bool dirty = false;
  int cx = 0, cy = 0; // cursor location
  struct termios orig_termios;
  int screenrows;
  int screencols;
  int rx = 0;
  erow *row;
  int rowoff = 0;
  int coloff = 0;
  int numrows = 0;
  char *filepath = nullptr;
  char statusmsg[80];
  time_t statusmsg_time = 0;

  editorConfig() { statusmsg[0] = '\0'; }

};

struct erow {
  int size = 0;
  char *chars = nullptr;
  int rsize = 0;
  char *render = nullptr;

  

  int rxToCx(int rx) const {
    int cur_rx = 0;
    for (int cx = 0; cx < this->size; cx++) {
      if (this->chars[cx] == '\t') {
        cur_rx += (NSPACES_PER_TAB - 1) - (cur_rx % NSPACES_PER_TAB);
      }
      cur_rx++;
      if (cur_rx > rx) { return cx; }
    }
    assert(false && "rx value that is out of range!");
    // return cx;
  }

  int cxToRx(int cx) const {
    int rx = 0;
    for (int j = 0; j < cx; ++j) {
      if (chars[j] == '\t') {
        rx += NSPACES_PER_TAB - (rx % NSPACES_PER_TAB);
      } else {
        rx++;
      }
    }
    return rx;
  }
  // should be private? since it updates info cache.
  void update(editorConfig &E) {

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
    E.dirty = true;
  }

  void insertChar(int at, int c, editorConfig &E) {
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
    E.dirty = true;
  }

  void appendString(char *s, size_t len, editorConfig &E) {
    chars = (char *)realloc(chars, size + len + 1);
    // copy string s into chars.
    memcpy(&chars[size], s, len);
    size += len;
    chars[size] = '\0';
    this->update(E);
    E.dirty = true;
  }
};

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

void  enableRawMode();
void  disableRawMode();

void write_to_child(const char *buf, int len);
int read_stdout_from_child(const char *buf, int bufsize);
int read_stderr_from_child(const char *buf, int bufsize);
// tactic mode goal.
void lean_server_get_tactic_mode_goal_state(LeanServerState state, LeanServerCursorInfo cinfo);
// term mode goal
void lean_server_get_term_mode_goal_state(LeanServerState state, LeanServerCursorInfo cinfo);
// autocomplete.
void lean_server_get_completion_at_point(LeanServerState state, LeanServerCursorInfo cinfo);
void editorSetStatusMessage(const char *fmt, ...);
char *editorPrompt(const char *prompt);
int clamp(int lo, int val, int hi);


/*** data ***/
extern editorConfig E; // from lib.


/*** terminal ***/
void die(const char *s);
int editorReadKey();
void getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);
void editorInsertRow(int at, const char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
void editorRowDelChar(erow *row, int at);
bool is_space_or_tab(char c);
void editorInsertNewline();
void editorInsertChar(int c);
void editorDelChar();
void editorOpen(const char *filename);
char *editorRowsToString(int *buflen);
void editorSave();
void editorFind();
void editorScroll();
void editorDrawRows(abuf &ab);
void editorDrawStatusBar(abuf &ab);
void editorDrawMessageBar(abuf &ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);
void editorMoveCursor(int key);
void editorProcessKeypress();
char *editorPrompt(const char *prompt);
void initEditor();
