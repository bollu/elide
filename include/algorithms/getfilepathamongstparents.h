#include <filesystem>
#include <optional>

namespace fs = std::filesystem;
// tries to find the path for `needle`, starting at directory `dirpath`,
// and returns NULL if file_path is NULL.
static std::optional<fs::path> getFilePathAmongstParents(fs::path dirpath, const char* needle)
{
    assert(dirpath.is_absolute());

    // only iterate this loop a bounded number of times.
    int NITERS = 1000;
    for (int i = 0; i < NITERS; ++i) {
        assert(i != NITERS - 1 && "ERROR: recursing when walking up parents to find `lakefile.lean`.");
        for (auto const& it : fs::directory_iterator { dirpath }) {
            if (it.path().filename() == needle) {
                return it.path();
            }
        }
        fs::path dirpath_parent = dirpath.parent_path();
        // we hit the root.
        if (dirpath_parent == dirpath) {
            break;
        }
        dirpath = dirpath_parent;
    }
    return {};
}
