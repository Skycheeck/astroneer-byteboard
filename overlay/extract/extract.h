#pragma once
#include <filesystem>
#include <string>
namespace rd::extract {
using LogFn = void (*)(const std::string& line);  // progress and diagnostics, UTF-8; may be null
struct Result { bool ok = false; std::string error; int items = 0; int icons = 0; };
// Reads the game's pak and writes catalog.tsv, taxonomy.tsv, icons.rgba, texts.tsv and languages.txt
// (formats in docs/PROTOCOL.md) into outDir (created if missing). Each file is written to a temp name then
// renamed over the old one.
// Never throws: a pak it cannot read gives ok = false and a readable error.
Result ExtractGameData(const std::filesystem::path& pak, const std::filesystem::path& outDir, LogFn log);
}
