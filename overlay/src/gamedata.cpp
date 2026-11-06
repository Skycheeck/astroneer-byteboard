#include "gamedata.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "extract/extract.h"
#include "paths.h"
#include "util.h"

namespace rd {

namespace {

constexpr const char* kStampFile = "source.txt";
constexpr const char* kStagingFolder = "new";
constexpr const wchar_t* kMainPak = L"pakchunk0-WindowsNoEditor.pak";
const char* const kDataFiles[] = {"catalog.tsv", "taxonomy.tsv", "icons.rgba"};

// The game's main pak: the game runs <game>/Astro/Binaries/Win64/<exe>, the pak is
// <game>/Astro/Content/Paks/pakchunk0-WindowsNoEditor.pak. When that name is missing, the first
// pakchunk0*.pak of the folder. Empty when there is none.
fs::path FindGamePak() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) return {};
    const fs::path paks = fs::path(buf).parent_path().parent_path().parent_path() / "Content" / "Paks";
    if (FileExists(paks / kMainPak)) return paks / kMainPak;
    std::error_code ec;
    for (fs::directory_iterator it(paks, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        if (name.rfind(L"pakchunk0", 0) == 0 && it->path().extension() == L".pak") return it->path();
    }
    return {};
}

// "<size> <last write time>" of a file; empty when it cannot be read.
std::string FileStamp(const fs::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) return {};
    const unsigned long long size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    const unsigned long long written =
        ((unsigned long long)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    return std::to_string(size) + " " + std::to_string(written);
}

// What the data was extracted from: the pak and this DLL file, so a game update and any new build
// of the DLL (a new extractor) extract again. Empty when the pak cannot be read.
std::string SourceStamp(const fs::path& pak, HMODULE self) {
    const std::string pakStamp = FileStamp(pak);
    if (pakStamp.empty()) return {};
    wchar_t dll[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(self, dll, (DWORD)std::size(dll));
    const std::string dllStamp = n == 0 || n >= std::size(dll) ? std::string("?") : FileStamp(dll);
    return "pak " + pakStamp + "\nbyteboard " RD_VERSION " " + dllStamp + "\n";
}

bool HaveDataFiles(const fs::path& folder) {
    for (const char* name : kDataFiles) {
        if (!FileExists(folder / name)) return false;
    }
    return true;
}

// Moves the three files of a finished extraction from the staging folder into data/. The stamp is
// removed first, so a move that fails half-way leaves data/ unstamped and the next start extracts
// again.
bool Publish(const fs::path& staging) {
    DeleteFileW((Paths().data / kStampFile).c_str());
    for (const char* name : kDataFiles) {
        if (!MoveFileExW((staging / name).c_str(), (Paths().data / name).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            Log(std::string("cannot move the extracted ") + name + " into data (error " + std::to_string(GetLastError()) + ")");
            return false;
        }
    }
    return true;
}

}  // namespace

bool EnsureGameData(HMODULE self, bool force) {
    if (!Paths().inGame) return HaveDataFiles(Paths().data);

    const fs::path pak = FindGamePak();
    const std::string stamp = pak.empty() ? std::string() : SourceStamp(pak, self);
    if (stamp.empty()) {
        Log("the game's pak was not found next to the game (Astro/Content/Paks), so the game data cannot be extracted");
        return HaveDataFiles(Paths().data);
    }
    std::string previous;
    if (!force && ReadSharedFile(Paths().data / kStampFile, previous) && previous == stamp && HaveDataFiles(Paths().data)) {
        return true;
    }

    // The extraction writes into data/new, so a failure never mixes new and old files in data/.
    const fs::path staging = Paths().data / kStagingFolder;
    std::error_code ec;
    fs::remove_all(staging, ec);
    Log("extracting the game data from " + Utf8(pak));
    const auto started = std::chrono::steady_clock::now();
    const extract::Result result =
        extract::ExtractGameData(pak, staging, [](const std::string& line) { Log("extract: " + line); });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const bool published = result.ok && Publish(staging);
    fs::remove_all(staging, ec);
    if (!published) {
        if (!result.ok) Log("game data extraction failed: " + result.error);
        if (!HaveDataFiles(Paths().data)) return false;
        Log("using the game data extracted before");
        return true;
    }
    char summary[96];
    snprintf(summary, sizeof summary, "extracted %d items and %d icons in %.1f s", result.items, result.icons, seconds);
    Log(summary);
    // Written last, so an interrupted extraction runs again on the next start.
    std::ofstream(Paths().data / kStampFile, std::ios::binary | std::ios::trunc) << stamp;
    return true;
}

}  // namespace rd
