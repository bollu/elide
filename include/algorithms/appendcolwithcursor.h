#pragma once
#include "datastructures/abuf.h"
#include "definitions/textareamode.h"
#include "algorithms/abufappendcodepointwithcursor.h"

// draw the text + cursor at position cursorIx into abuf 'dst'.
//   dst: destination buffer to append into.
//   row : the row to be drawn.
//   colIx : index of the column to be drawn. `0 <= colIx <= row.ncodepoints()`.
//   cursorIx : index of cursor, of value `0 <= cursorIx <= row.ncodepoints()`.
//   tam : text area mode, affects if the cursor will be bright or dull.
static void appendColWithCursor(abuf* dst, const abuf* row,
    Size<Codepoint> colIx,
    Size<Codepoint> cursorIx,
    TextAreaMode tam)
{
    assert(cursorIx <= row->ncodepoints());
    assert(colIx <= row->ncodepoints());
    const bool cursorAtCol = colIx == cursorIx;

    if (colIx < row->ncodepoints()) {
        const char* codepoint = row->getCodepoint(colIx.toIx());
        if (cursorAtCol) {
            abufAppendCodepointWithCursor(dst, tam, codepoint);
        } else {
            dst->appendCodepoint(codepoint);
        }
    } else {
        assert(colIx == row->ncodepoints());
        if (cursorAtCol) {
            abufAppendCodepointWithCursor(dst, tam, " ");
        }
        // nothing to render here, just a blank space.
    }
}
