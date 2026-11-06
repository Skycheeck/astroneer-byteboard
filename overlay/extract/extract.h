#pragma once
#include <filesystem>
#include <string>
namespace rd::extract {
using LogFn = void (*)(const std::string& line);  // progress and diagnostics, UTF-8; may be null
struct Result { bool ok = false; std::string error; int items = 0; int icons = 0; };
// Reads the pak and writes catalog.tsv, taxonomy.tsv and icons.rgba into outDir (created if missing),
// byte-identical to `node tools/extract.mjs`. Each file is written to a temp name then renamed over the
// old one. Never throws.
Result ExtractGameData(const std::filesystem::path& pak, const std::filesystem::path& outDir, LogFn log);
}
