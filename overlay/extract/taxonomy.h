#pragma once
// Groups, printers and the rules that decide which catalog items are research entries and where the
// overlay lists them. This is the one source of them: the overlay gets groups and printers through
// taxonomy.tsv.
#include <span>
#include <string_view>

namespace rd::extract {

// Labels come in Russian and in English (the ...En fields); the overlay follows the game's language.

// A functional group that cuts across printers.
struct Group {
    const char* key;
    const char* label;
    const char* labelEn;
};

// key is the catalog's printer value, category the game's ItemCategory asset name, shortLabel the
// lower-case word used inside sentences.
struct Printer {
    const char* key;
    const char* category;
    const char* label;
    const char* shortLabel;
    const char* labelEn;
    const char* shortLabelEn;
};

// In display order.
std::span<const Group> Groups();
std::span<const Printer> Printers();

// The printer whose ItemCategory asset is named category ("ItemCategory_SmFab"), or nullptr.
const Printer* FindPrinter(std::string_view category);

struct Classification {
    const char* group = nullptr;  // null when no rule matches
    const char* subgroup = nullptr;
    const char* subgroupEn = nullptr;
};

// The first rule matching the English name wins, then the first rule matching the asset id.
Classification Classify(std::string_view nameEn, std::string_view id);

// Catalog items that are not research entries of their own (kExclude).
bool IsExcluded(std::string_view id);

// Creative-only items the game hides in code rather than with bIsCreativeModeOnly.
bool IsCreativeOnly(std::string_view id);

}  // namespace rd::extract
