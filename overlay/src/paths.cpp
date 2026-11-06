#include "paths.h"

#include <iterator>
#include <string>

#include "util.h"

namespace rd {

namespace {

ModPaths g_paths;

// This DLL's full path; empty on failure.
std::wstring ModulePath(HMODULE self) {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(self, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) return {};
    return std::wstring(buf, n);
}

// The folder of a path with backslashes ("C:\a\b.dll" -> "C:\a"); empty when it has none.
std::wstring FolderOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

void CreateModFolders(HMODULE self) {
    const std::wstring mod = FolderOf(ModulePath(self));
    if (mod.empty() || !IsFile(mod + L"\\Scripts\\main.lua")) return;
    for (const wchar_t* name : {L"\\in", L"\\out", L"\\data"}) CreateDirectoryW((mod + name).c_str(), nullptr);
}

bool InitPaths(HMODULE self) {
    const std::wstring dll = ModulePath(self);
    if (dll.empty()) return false;
    const fs::path dir = fs::path(dll).parent_path();
    fs::path mod;
    if (FileExists(dir / "Scripts" / "main.lua")) {
        mod = dir;  // the game DLL: <mod>/Byteboard.dll
        g_paths.inGame = true;
    } else if (const fs::path repoMod = dir.parent_path().parent_path() / "mod" / "Byteboard";
               FileExists(repoMod / "Scripts" / "main.lua")) {
        mod = repoMod;  // the preview DLL: <repo>/overlay/bin/ByteboardPreview.dll
    } else {
        return false;
    }
    g_paths.mod = mod;
    g_paths.out = mod / "out";
    g_paths.in = mod / "in";
    g_paths.data = mod / "data";
    // DllMain already made them for the game DLL; the preview DLL needs them for its log.
    std::error_code ec;
    for (const fs::path* folder : {&g_paths.out, &g_paths.in, &g_paths.data}) fs::create_directories(*folder, ec);
    return true;
}

const ModPaths& Paths() {
    return g_paths;
}

}  // namespace rd
