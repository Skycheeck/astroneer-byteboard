#include "paths.h"

#include <iterator>

#include "util.h"

namespace rd {

namespace {

RepoPaths g_paths;

}  // namespace

bool InitPaths(HMODULE self) {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(self, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) return false;
    // <repo>/ue4ss/out/X.dll or <repo>/overlay/bin/X.dll: the repository is two folders above the DLL's.
    const fs::path root = fs::path(buf).parent_path().parent_path().parent_path();
    if (!FileExists(root / "overlay" / "catalog.tsv")) return false;
    g_paths.root = root;
    g_paths.out = root / "ue4ss" / "out";
    g_paths.in = root / "ue4ss" / "in";
    g_paths.overlay = root / "overlay";
    return true;
}

const RepoPaths& Paths() {
    return g_paths;
}

}  // namespace rd
