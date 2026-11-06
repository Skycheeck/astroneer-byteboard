#pragma once
// The game's texts: compiled localization files (.locres, tools/lib/locres.mjs), cooked string tables
// (tools/lib/stringtable.mjs), and the lookup the catalog step does with them (the localizer in
// tools/steps/build-catalog.mjs).
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.h"
#include "pak.h"
#include "value.h"

namespace rd::extract {

enum class Language { En, Ru };

class Localizer {
public:
    // Reads the en and ru Game.locres; throws when either cannot be read.
    Localizer(const Pak& pak, const Logger& log);

    // Text lookup as the game does it: a string table entry is the .locres string under the table's
    // namespace and the entry key, falling back to the table's source string, then to the key; a base
    // text is the .locres string under its own namespace and key, falling back to its source string.
    // Empty for no text (null or undefined).
    std::optional<std::string> Localize(const Value* text, Language language);

    // Logs how many texts fell back to their source string and which table entries were not found.
    void ReportMisses() const;

private:
    // namespace -> key -> localized string; a missing string is stored as "" (both are falsy in JS).
    using Locres = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
    struct StringTable {
        std::string ns;
        std::unordered_map<std::string, std::string> entries;  // key -> source string
    };

    static Locres ParseLocres(ByteView data);
    StringTable ReadStringTable(const std::string& tablePath) const;
    const StringTable* StringTableAt(const std::string& tablePath);
    static const std::string* Find(const Locres& locres, const std::string& ns, const std::string& key);

    const Pak& pak_;
    const Logger& log_;
    Locres en_;
    Locres ru_;
    std::unordered_map<std::string, std::optional<StringTable>> tables_;  // empty when unreadable
    // For ReportMisses: "<namespace or table> <key>" of the texts that fell back or were not found.
    std::unordered_set<std::string> untranslated_[2];
    std::unordered_set<std::string> unresolved_;
    std::vector<std::string> unresolvedInOrder_;
};

}  // namespace rd::extract
