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

int main(int argc, char **argv){
  enableRawMode();
  initEditor();

  // E.lean_sever_state = LeanServerState::init(LST_LEAN_SERVER); // start lean --server.  
  editorSetStatusMessage("HELP: Ctrl-Q = quit");

  char *filepath = NULL;
  if (argc >= 2) {
    filepath = argv[1];
  } else {
    filepath = strdup("/tmp/edtr-scratch");
  }
  editorOpen(filepath);

  while (1) {
    initEditor();
    editorSave();
    editorRefreshScreen();
    editorProcessKeypress();
  };

  return 0;
}
