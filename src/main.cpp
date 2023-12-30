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
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char **argv){
  // make stdin non blocking.
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

  enableRawMode();
  initEditor();

  disableRawMode();

  char *path = NULL;
  if (argc >= 2) {
    path = argv[1];
  }

  std::string path_str(fs::path(path).parent_path());
  g_editor.original_cwd = abuf::from_copy_str(path_str.c_str());  

  if (path && fs::is_regular_file(path)) {
    g_editor.openNewFile(path);
  }
  // look for lakefile.lean in directory parents. if available,
  // then start lean server there. 
  // If unavailable, then start lean server with lean --server.

  // fileConfigOpen(&g_editor.curFile, filepath); // TODO: refactor to use curFile.
  // fileConfigLaunchLeanServer(&g_editor.curFile);
  // fileConfigSyncLeanState(&g_editor.curFile);

  enableRawMode();
  while (1) {
    editorDraw();
    editorProcessKeypress();
    const long MICRO_TO_MILLI = 1000;
    usleep(MICRO_TO_MILLI);

    // printf("processing keypress..\n");
  };
  disableRawMode();
  return 0;
}
