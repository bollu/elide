#pragma once
#include <vector>
#include "datastructures/cursor.h"
#include <filesystem>
#include "datastructures/undoer.h"
#include "datastructures/leanserverstate.h"
#include "definitions/infoviewtab.h"
#include "lean_lsp.h"

namespace fs = std::filesystem;
// NOTE:
// TextDocument for LSP does not need to be cached, as its value monotonically increases,
// even during undo/redo.
struct FileConfigUndoState {
    std::vector<abuf> rows;
    Cursor cursor;
    int cursor_render_col = 0;
    int scroll_row_offset = 0;
    int scroll_col_offset = 0;

    // TODO: should we use a different fn rather than `==`?
    bool operator==(const FileConfigUndoState& other) const
    {
        return rows == other.rows; // only file state is part of undo state for debouncing.
    }
};

fs::path ctrlpGetGoodRootDirAbsolute(const fs::path absolute_startdir);

// NOTE: in sublime text, undo/redo is a purely *file local* idea.
// Do I want a *global* undo/redo? Probably not, no?
// TODO: think about just copying the sublime text API :)
struct FileLocation;
struct FileConfig : public Undoer<FileConfigUndoState> {
    FileConfig(FileLocation loc);

    // offset for scrolling.
    int scroll_row_offset = 0;
    // column offset for scrolling. will render from col_offset till endof row.
    // TODO: this should count in codepoints?
    int scroll_col_offset = 0;

    fs::path absolute_filepath;

    // lean server for file.
    LeanServerState lean_server_state;

    // TextDocument for LSP
    int lsp_file_version = -1;

    // diagonstics from LSP.
    std::vector<LspDiagnostic> lspDiagnostics;

    Cursor leanInfoViewRequestedCursor; // the cursor at which the info view was requested.
    LspNonblockingResponse leanInfoViewPlainGoal;
    LspNonblockingResponse leanInfoViewPlainTermGoal;
    LspNonblockingResponse leanHoverViewHover;
    // TODO: implement definition
    InfoViewTab infoViewTab = IVT_Tactic;
    LspNonblockingResponse leanGotoRequest;
    // TODO: implement completion.
    LspNonblockingResponse leanCompletionViewCompletion;

    // file progress from LSP.
    // contains the start and end ranges, and whether the processing is finished.
    struct ProgressBar {
        int startRow = 0;
        int endRow = 0;
        bool finished = false;
    };
    ProgressBar progressbar;

    bool isSaveDirty() const
    {
        return this->_is_dirty_save;
    }

    bool isLeanSyncDirty() const
    {
        return this->_is_dirty_lean_sync;
    }

    void undirtyLeanSync()
    {
        this->_is_dirty_lean_sync = false;
    }
    // if 'b' is true, then mark the state as dirty.
    // if 'b' is false, then leave the dirty state as-is.
    void makeDirty()
    {
        this->_is_dirty_save = true;
        this->_is_dirty_lean_sync = true;
    }

    bool whenDirtySave()
    {
        bool out = _is_dirty_save;
        _is_dirty_save = false;
        return out;
    }

private:
    bool _is_dirty_save = true;
    bool _is_dirty_lean_sync = true;
};

void fileConfigSyncLeanState(FileConfig* file_config);
void fileConfigLaunchLeanServer(FileConfig* file_config);
