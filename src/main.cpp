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

  if (argc >= 2) { g_editor.original_cwd = fs::path(argv[1]); }

  if (argc >= 2) { 
    g_editor.original_cwd = fs::canonical(fs::path(argv[1]));
  } else {
    g_editor.original_cwd = fs::absolute(fs::current_path());
  }

  if (fs::is_regular_file(g_editor.original_cwd)) {
    const fs::path filepath = g_editor.original_cwd;
    g_editor.original_cwd = g_editor.original_cwd.remove_filename();
    g_editor.original_cwd = ctrlpGetGoodRootDirAbsolute(g_editor.original_cwd);
    g_editor.getOrOpenNewFile(FileLocation(filepath, Cursor(0, 0)));
    g_editor.getOrOpenNewFile(FileLocation(filepath, Cursor(0, 0)));
  } else {
    g_editor.original_cwd = ctrlpGetGoodRootDirAbsolute(g_editor.original_cwd);
    ctrlpOpen(&g_editor.ctrlp, VM_NORMAL, g_editor.original_cwd);
  }

  tilde::tildeWrite("original_cwd: '%s'", g_editor.original_cwd.c_str()); 

  enableRawMode();
  while (1) {
    timespec tbegin;
    Debouncer::get_time(&tbegin);

    editorDraw();
    editorProcessKeypress();
    editorTickPostKeypress();

    timespec tend;
    Debouncer::get_time(&tend);

    const long elapsed_nanosec = tend.tv_nsec - tbegin.tv_nsec;
    const long elapsed_sec = tend.tv_sec - tbegin.tv_sec;
    if (elapsed_sec > 0) { continue; }

    const long elapsed_microsec = elapsed_nanosec / 1000;
    const long total_microsec = 1000000 / 120; // 120 FPS = 1s / 120 frames = 1000000 microsec / 120 frames 
    usleep(clamp0(total_microsec - elapsed_microsec));
  };
  disableRawMode();
  return 0;
}
