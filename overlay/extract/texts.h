#pragma once
// The game's texts: compiled localization files (Localization/Game/<locale>/Game.locres), cooked
// string tables, and the lookup of a text value (uasset.h) in them. Languages are named by their
// locale folder ("en", "ru", "pt-BR").
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.h"
#include "pak.h"
#include "value.h"

namespace rd::extract {

// The languages catalog.tsv carries.
inline const std::string kEnglish = "en";
inline const std::string kRussian = "ru";

// The locale folders under Localization/Game/ that have a Game.locres, sorted.
std::vector<std::string> GameLanguages(const Pak& pak);

class Localizer {
public:
    // Reads the English and Russian Game.locres; throws when either cannot be read.
    Localizer(const Pak& pak, const Logger& log);

    // Reads another language's Game.locres. False, after logging why, when the pak has none or it
    // cannot be read.
    bool AddLanguage(const std::string& locale);

    // Text lookup as the game does it: a string table entry is the .locres string under the table's
    // namespace and the entry key, falling back to the table's source string, then to the key; a base
    // text is the .locres string under its own namespace and key, falling back to its source string.
    // Empty for no text (null or missing).
    std::optional<std::string> Localize(const Value* text, const std::string& locale);

    // The same lookup without the fallback to a string table key: empty when the text is missing,
    // empty or cannot be found.
    std::optional<std::string> Translate(const Value* text, const std::string& locale);

    // Only the language's own string, without the fallback to the (English) source string: empty when
    // the language has no translation of the text. A culture-invariant text is the same in every language.
    std::optional<std::string> InLanguage(const Value* text, const std::string& locale);

    // Logs how many texts fell back to their source string and which table entries were not found.
    void ReportMisses() const;

private:
    // namespace -> key -> localized string; a missing string is stored as "", which never counts as found.
    using Locres = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
    struct StringTable {
        std::string ns;
        std::unordered_map<std::string, std::string> entries;  // key -> source string
    };
    enum class Fallback { None, Source, Key };

    std::optional<std::string> Resolve(const Value* text, const std::string& locale, Fallback fallback);
    static Locres ParseLocres(ByteView data);
    StringTable ReadStringTable(const std::string& tablePath) const;
    const StringTable* StringTableAt(const std::string& tablePath);
    static const std::string* Find(const Locres& locres, const std::string& ns, const std::string& key);

    const Pak& pak_;
    const Logger& log_;
    std::map<std::string, Locres> locres_;                                // by locale
    std::unordered_map<std::string, std::optional<StringTable>> tables_;  // empty when unreadable
    // For ReportMisses: "<namespace or table> <key>" of the texts that fell back or were not found.
    std::map<std::string, std::unordered_set<std::string>> untranslated_;  // by locale
    std::unordered_set<std::string> unresolved_;
    std::vector<std::string> unresolvedInOrder_;
};

}  // namespace rd::extract
