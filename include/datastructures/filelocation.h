#pragma once
#include <filesystem>
#include "cursor.h"
#include "datastructures/fileconfig.h"

namespace fs = std::filesystem;
// represents a file plus a location, used to save history of file opening/closing.
struct FileLocation {
    fs::path absolute_filepath;
    Cursor cursor;

    // completion view needs this default ctor :(
    // TODO: ask sebastian how to avoid this.
    FileLocation() { }

    FileLocation(const FileConfig& file)
        : absolute_filepath(file.absolute_filepath)
        , cursor(file.cursor)
    {
    }

    FileLocation(const FileLocation& other)
    {
        *this = other;
    }

    FileLocation(fs::path absolute_filepath, Cursor cursor)
        : absolute_filepath(absolute_filepath)
        , cursor(cursor) {};

    FileLocation& operator=(const FileLocation& other)
    {
        this->absolute_filepath = other.absolute_filepath;
        this->cursor = other.cursor;
        return *this;
    }

    bool operator==(const FileLocation& other) const
    {
        return cursor == other.cursor && absolute_filepath == other.absolute_filepath;
    }
};

