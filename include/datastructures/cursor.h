#pragma once
#include "mathutil.h"

struct Cursor {
    int row = 0; // index of row. Must be within [0, file->nrows].
    Size<Codepoint> col = Size<Codepoint>(0); // #graphemes to move past from the start of the row to get to the current one.

    Cursor() = default;
    Cursor(int row, int col)
        : row(row)
        , col(Size<Codepoint>(col)) {};

    bool operator==(const Cursor& other) const
    {
        return row == other.row && col == other.col;
    }

    bool operator!=(const Cursor& other) const
    {
        return !(*this == other);
    }
};