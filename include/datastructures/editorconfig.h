#pragma once
#include "datastructures/zipper.h"
#include "definitions/vimmode.h"
#include "views/completion.h"
#include "views/compile.h"
#include "views/tilde.h"
#include "datastructures/abbreviationdict.h"

struct EditorConfig {
    Zipper<FileLocation> file_location_history;
    VimMode vim_mode = VM_NORMAL;
    int screenrows = 0;
    int screencols = 0;

    char statusmsg[80];
    time_t statusmsg_time = 0;

    fs::path original_cwd; // cwd that the process starts in.
    CtrlPView ctrlp;
    CompletionView completion;
    compileView::CompileView compileView;
    AbbreviationDict abbrevDict;

    EditorConfig() { statusmsg[0] = '\0'; }

    FileConfig* curFile()
    {
        if (files.size() == 0) {
            return NULL;
        } else {
            assert(fileIx < files.size());
            return &files[fileIx];
        }
    };

    void getOrOpenNewFile(FileLocation file_loc, bool isUndoRedo = false)
    {
        tilde::tildeWrite("%s %s:%d:%d", __FUNCTION__, file_loc.absolute_filepath.c_str(),
            file_loc.cursor.row,
            file_loc.cursor.col.size);
        assert(file_loc.absolute_filepath.is_absolute());

        if (!isUndoRedo) {
            if (this->fileIx != -1) {
                assert(this->fileIx >= 0);
                assert(this->fileIx < this->files.size());
                file_location_history.push_back(FileLocation(this->files[this->fileIx]));
            }
            file_location_history.push_back(file_loc);
        }

        // look if file exists.
        // for(int i = 0; i < this->files.size(); ++i) {
        //   if (this->files[i].absolute_filepath == file_loc.absolute_filepath) {
        //     assert(false && "found an existing file");
        //     this->fileIx = i;
        //     this->files[this->fileIx].cursor = file_loc.cursor;
        //     return;
        //   }
        // }
        // we were unable to find an already open file, so make a new file.
        this->files.push_back(FileConfig(file_loc));
        this->fileIx = this->files.size() - 1;
        this->files[this->fileIx].cursor = file_loc.cursor;
    }

    void undoFileMove()
    {
        const int prevIx = file_location_history.getIx();
        file_location_history.left();
        if (prevIx != file_location_history.getIx()) {
            // state changed, so we must have a file of interest.
            this->getOrOpenNewFile(*file_location_history.getFocus(), /*isUndoRedo=*/true);
        }
    }

    void redoFileMove()
    {
        const int prevIx = file_location_history.getIx();
        file_location_history.right();
        if (prevIx != file_location_history.getIx()) {
            // state changed, so we must have a file of interest.
            this->getOrOpenNewFile(*file_location_history.getFocus(), /*isUndoRedo=*/true);
        }
    }

private:
    std::vector<FileConfig> files;
    int fileIx = -1;
};

extern EditorConfig g_editor; // global editor handle.