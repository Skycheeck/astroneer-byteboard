#pragma once
// The game data the overlay reads (<mod>/data: catalog.tsv, taxonomy.tsv, icons.rgba), extracted from
// the game's own pak by extract/ (no game files ship with the mod).
// Threads: EnsureGameData runs on the overlay's MainThread, after InitPaths and before LoadCatalog.
#include <windows.h>

namespace rd {

// In the game: extracts the data when <mod>/data/source.txt does not match the pak and this DLL
// (first start, game update, new build), or always with force. True when <mod>/data has a catalog
// to load afterwards, including the data of an earlier extraction when a new one fails. The preview
// DLL only checks that the files exist.
bool EnsureGameData(HMODULE self, bool force);

}  // namespace rd
