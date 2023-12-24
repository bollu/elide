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

int main(int argc, char **argv){
  enableRawMode();
  initEditor();
  disableRawMode();

  char *filepath = NULL;
  if (argc >= 2) {
    filepath = argv[1];
  } else {
    filepath = strdup("/tmp/edtr-scratch");
  }

  // look for lakefile.lean in directory parents. if available,
  // then start lean server there. 
  // If unavailable, then start lean server with lean --server.

  editorOpen(filepath); // TODO: refactor to use curFile.
  fileConfigLaunchLeanServer(&g_editor.curFile);
  fileConfigSyncLeanState(&g_editor.curFile);

  enableRawMode();
  while (1) {
    editorDraw();
    editorProcessKeypress();
  };

  return 0;
}
