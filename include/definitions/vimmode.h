#pragma once

enum VimMode {
    VM_NORMAL, // mode where code is only viewed and locked for editing.
    VM_INSERT, // mode where code is edited.
    VM_INFOVIEW_DISPLAY_GOAL, // mode where infoview is shown.
    VM_COMPLETION, // mode where code completion results show up.
    VM_CTRLP, // mode where control-p search anything results show up.
    VM_TILDE, // mode where the editor logs its info, like the infamous quake `~`.
    VM_COMPILE, // mode where `lake build` is called.
};
