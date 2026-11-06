#pragma once
// Where the repository is.
// Threads: InitPaths runs once on the overlay's MainThread before any other overlay code; every
// thread only reads the paths afterwards.
#include <windows.h>

#include <filesystem>

namespace rd {

struct RepoPaths {
    std::filesystem::path root;     // the repository
    std::filesystem::path out;      // ue4ss/out: the mod's files, overlay.log, overlay.ini
    std::filesystem::path in;       // ue4ss/in: cmd.txt, allow-unlock
    std::filesystem::path overlay;  // overlay: catalog.tsv, taxonomy.tsv, icons.rgba
};

// Finds the repository from this DLL's location: the mod loads ue4ss/out/ResearchOverlay-active.dll
// (its copy of overlay/bin/ResearchOverlay.dll) and preview.exe loads overlay/bin/ResearchOverlayPreview.dll.
// False when that folder has no overlay/catalog.tsv (not a checkout with extracted data); the
// overlay then stays off.
bool InitPaths(HMODULE self);
const RepoPaths& Paths();

}  // namespace rd
