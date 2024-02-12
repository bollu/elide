#pragma once

#include "datastructures/abuf.h"
#include "definitions/textareamode.h"
#include "mathutil.h"
#include "SDL_events.h"

struct SingleLineTextArea {
    TextAreaMode mode = TextAreaMode::TAM_Normal;
    abuf text;
    Size<Codepoint> col = 0;
};

void singleLineTextAreaDraw(SingleLineTextArea* area);
void _singleLineTextAreaHandleInput(SingleLineTextArea* area);
