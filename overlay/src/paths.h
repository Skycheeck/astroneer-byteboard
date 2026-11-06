#pragma once
// Where the mod folder is.
// Threads: CreateModFolders runs inside DllMain; InitPaths runs once on the overlay's MainThread
// before any other overlay code; every thread only reads the paths afterwards.
#include <windows.h>

#include <filesystem>

namespace rd {

struct ModPaths {
    std::filesystem::path mod;   // the mod folder (ue4ss/Mods/Byteboard, or mod/Byteboard in the repository)
    std::filesystem::path out;   // <mod>/out: the Lua mod's files, overlay.log, overlay.ini
    std::filesystem::path in;    // <mod>/in: cmd.txt, allow-unlock
    std::filesystem::path data;  // <mod>/data: catalog.tsv, taxonomy.tsv, icons.rgba (extracted game data)
    bool inGame = false;         // the game DLL in its mod folder (false: the preview DLL in overlay/bin)
};

// Creates <mod>/in, <mod>/out and <mod>/data when this DLL sits in a mod folder (next to
// Scripts/main.lua). Called from DllMain, so the Lua mod, which loads the DLL before it writes
// anything, finds the folders; it uses only kernel32 calls.
void CreateModFolders(HMODULE self);

// Finds the mod folder from this DLL's location: the game DLL is <mod>/Byteboard.dll; the preview
// DLL is <repo>/overlay/bin/ByteboardPreview.dll and uses <repo>/mod/Byteboard. False when neither
// layout matches; the overlay then stays off.
bool InitPaths(HMODULE self);
const ModPaths& Paths();

}  // namespace rd
