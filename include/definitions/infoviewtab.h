#pragma once
// TODO: Rename to InfoViewTabKind.
enum InfoViewTab {
    IVT_Tactic, // show the current tactic state in the info view.
    IVT_Hover,
    IVT_Messages, // show messages form the LSP server in the info view.
    IVT_NumTabs
};
