#pragma once
#include "datastructures/abuf.h"
#include "definitions/vimmode.h"
#include "lean_lsp.h"
#include "mathutil.h"
#include "views/ctrlp.h"
#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <json-c/json.h>
#include <json-c/json_util.h>
#include <map>
#include <optional>
#include <stack>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include "subprocess.h"
#include "datastructures/cursor.h"
#include "datastructures/filelocation.h"
#include "views/ctrlp.h"
#include "datastructures/fileconfig.h"
namespace fs = std::filesystem;

static int utf8_next_code_point_len(const char* str);

static const char* VERSION = "0.0.1";


struct FileConfig;
static InfoViewTab infoViewTabCycleNext(FileConfig* f, InfoViewTab t);
static InfoViewTab infoViewTabCyclePrevious(FileConfig* f, InfoViewTab t);

// returns true if it was filled in this turn.
static bool whenFillLspNonblockingResponse(LeanServerState& state, LspNonblockingResponse& o)
{
    if (o.response.has_value()) {
        return false;
    }

    auto it = state.request2response.find(o.request);
    if (it != state.request2response.end()) {
        o.response = it->second;
        return true;
    }
    return false;
}


void enableRawMode();
void disableRawMode();

void write_to_child(const char* buf, int len);
int read_stdout_from_child(const char* buf, int bufsize);
int read_stderr_from_child(const char* buf, int bufsize);
void editorSetStatusMessage(const char* fmt, ...);
char* editorPrompt(const char* prompt);

/*** terminal ***/
void die(const char* fmt, ...);
int editorReadKey();
void getCursorPosition(int* rows, int* cols);
int getWindowSize(int* rows, int* cols);
void fileConfigInsertRowBefore(FileConfig* f, int at, const char* s, size_t len);
void editorDelRow(int at);
// Delete character at location `at`.
// Invariant: `at in [0, row->size)`.
bool is_space_or_tab(char c);
void fileConfigInsertEnterKey(FileConfig* f);
void fileConfigInsertCharBeforeCursor(FileConfig* f, int c); // 32 bit.
void fileConfigDelChar(FileConfig* f);
std::string fileConfigRowsToCppString(FileConfig* file);
void fileConfigRowsToBuf(FileConfig* f, abuf* buf);
void fileConfigDebugPrint(FileConfig* f, abuf* buf);
void fileConfigCursorMoveWordNext(FileConfig* f);
void fileConfigCursorMoveWordPrevious(FileConfig* f);
void fileConfigSave(FileConfig* f);
void fileConfigGotoDefinitionNonblocking(FileConfig* f);
LspPosition cursorToLspPosition(Cursor c);

void editorDraw();
void editorTickPostKeypress();
void editorScroll();
void editorDrawRows(abuf& ab);
void editorDrawStatusBar(abuf& ab);
void editorDrawMessageBar(abuf& ab);
void editorMoveCursor(int key);
void editorProcessKeypress();
char* editorPrompt(const char* prompt);
void initEditor();

// get the path to the executable, so we can build the path to resources.
fs::path get_executable_path();
// get the path to `/path/to/exe/abbreviations.json`.
fs::path get_abbreviations_dict_path();
