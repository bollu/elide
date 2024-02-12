#pragma once
#include <vector>
#include <string>
#include "stdio.h"
#include "SDL2/SDL_events.h"
#include "definitions/vimmode.h"

namespace tilde {
struct TildeView {
    int scrollback_ix = 0;
    bool quitPressed = false;
    std::vector<std::string> log;
    FILE* logfile = nullptr;
    VimMode previousMode;
};

extern TildeView g_tilde;

bool tildeWhenQuit(TildeView* tilde);
void tildeOpen(TildeView* tilde);
void tildeHandleInput(TildeView* tilde, const SDL_Event &e);
void tildeDraw(TildeView* tilde);
// this sucks, global state.
// void tildeWrite(std::string str);
void tildeWrite(const char* fmt, ...);
void tildeWrite(const std::string& str);
void tildeWrite(const abuf& buf);
};
