#pragma once
// Groups, printers and the rules that decide which catalog items are research entries
// (tools/lib/taxonomy.mjs, the one source of these; keep the two in step).
#include <span>
#include <string_view>

namespace rd::extract {

// A functional group that cuts across printers.
struct Group {
    const char* key;
    const char* label;
};

// key is the catalog's printer value, category the game's ItemCategory asset name, shortLabel the
// lower-case word used inside sentences.
struct Printer {
    const char* key;
    const char* category;
    const char* label;
    const char* shortLabel;
};

// In display order.
std::span<const Group> Groups();
std::span<const Printer> Printers();

struct Classification {
    const char* group = nullptr;  // null when no rule matches
    const char* subgroup = nullptr;
};

// The first rule matching the English name wins, then the first rule matching the asset id.
Classification Classify(std::string_view nameEn, std::string_view id);

// Catalog items that are not research entries of their own (EXCLUDE).
bool IsExcluded(std::string_view id);

// Items that can never be researched carry a huge price instead; anything from this cost up is not a
// research entry.
constexpr double kNotResearchableCost = 1e8;

// Creative-only items the game hides in code rather than with bIsCreativeModeOnly.
bool IsCreativeOnly(std::string_view id);

}  // namespace rd::extract
