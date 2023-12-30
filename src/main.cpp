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
  // make stdin non blocking.
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

  enableRawMode();
  initEditor();

  disableRawMode();

  const char *path = NULL;
  if (argc >= 2) { path = argv[1]; }

  g_editor.original_cwd = fs::current_path().parent_path();  
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
    timespec tbegin;
    Debouncer::get_time(&tbegin);

    editorDraw();
    editorProcessKeypress();


    timespec tend;
    Debouncer::get_time(&tend);

    const long elapsed_nanosec = tend.tv_nsec - tbegin.tv_nsec;
    const long elapsed_sec = tend.tv_sec - tbegin.tv_sec;
    if (elapsed_sec > 0) { continue; }

    const long elapsed_microsec = elapsed_nanosec / 1000;
    const long total_microsec = 1000000 / 60; // 30 FPS = 1s / 30 frames = 1000000 microsec / 30 frames 
    usleep(clamp0(total_microsec - elapsed_microsec));
  };
  disableRawMode();
  return 0;
}
