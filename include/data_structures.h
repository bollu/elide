struct LeanServerCursorInfo {
  const char *file_path;
  int row;
  int col;
};


#define PIPE_WRITE_IX 1
#define PIPE_READ_IX 0

// https://tldp.org/LDP/lpg/node11.html
struct LeanServerState {
  int parent_buffer_to_child_stdin[2];
  int child_stdout_to_parent_buffer[2];
  int child_stderr_to_parent_buffer[2];
  pid_t childpid;
};

enum LeanServerInitKind {
    LST_LEAN_SERVER, // lean --servver
    LST_LAKE_SERVE, // lake serve
}; 


static const int NSPACES_PER_TAB = 2;
const char *VERSION = "0.0.1";


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
