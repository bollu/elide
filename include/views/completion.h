#pragma once
#include "datastructures/lspnonblockingresponse.h"
#include "datastructures/singlelinetextarea.h"
#include "definitions/vimmode.h"
#include "datastructures/fileconfig.h"
#include "SDL2/SDL_events.h"

struct CompletionView {
    // TODO: rename to LspNonblockingResponse
    LspNonblockingResponse completionResponse;
    SingleLineTextArea textArea;
    VimMode previous_state;

    struct Item {
        std::string detail;
        std::string doc;
        int kind;
        std::string label;

        Item(std::string detail, std::string doc, int kind, std::string label)
            : detail(detail)
            , doc(doc)
            , kind(kind)
            , label(label)
        {
        }
    };
    std::vector<Item> items;
    int itemIx = -1;
    bool quitPressed = false;
    bool selectPressed = false;
};

bool completionWhenQuit(CompletionView* view);
bool completionWhenSelected(CompletionView* view);
void completionOpen(CompletionView* view, VimMode previous_state, FileConfig* f);
void completionHandleInput(CompletionView* view, const SDL_Event &e);
void completionTickPostKeypress(FileConfig* f, CompletionView* view);
void completionDraw(CompletionView* view);
