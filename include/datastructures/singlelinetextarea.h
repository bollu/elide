#pragma once

#include "datastructures/abuf.h"
#include "definitions/textareamode.h"
#include "mathutil.h"

struct SingleLineTextArea {
    TextAreaMode mode = TextAreaMode::TAM_Normal;
    abuf text;
    Size<Codepoint> col = 0;
};

void SingleLineTextAreaHandleInput(SingleLineTextArea* area, int c);
