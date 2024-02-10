#pragma once
#include "datastructures/abuf.h"
#include "definitions/textareamode.h"
#include "definitions/escapecode.h"

// draw the text 'codepoint', plus the cursor backgrounding in text area mode 'tam' into
// abuf 'dst'.
static void abufAppendCodepointWithCursor(abuf* dst, TextAreaMode tam, const char* codepoint)
{
    if (tam == TAM_Normal) {
        dst->appendstr(ESCAPE_CODE_CURSOR_NORMAL);
    } else {
        assert(tam == TAM_Insert);
        dst->appendstr(ESCAPE_CODE_CURSOR_INSERT);
    }
    dst->appendCodepoint(codepoint); // draw the text.
    dst->appendstr(ESCAPE_CODE_UNSET);
}

