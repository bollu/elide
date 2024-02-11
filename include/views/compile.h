#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "SDL2/SDL_events.h"

namespace fs = std::filesystem;

namespace compileView {
struct CompileView {
    int scrollback_ix = 0;
    bool quitPressed = false;
    struct Item {
        fs::path filepath;
        int row;
        int col;
        std::string info;
    };
    // log
    std::vector<std::string> log;
};

bool compileViewWhenQuit(CompileView* view);
void compileViewOpen(CompileView* view);
void compileViewHandleInput(CompileView* view, const SDL_Event &e);
void compileViewDraw(CompileView* view);
};
