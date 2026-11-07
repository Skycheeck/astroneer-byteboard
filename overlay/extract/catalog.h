#pragma once
// The research catalog: item types and missions (scan.h) -> catalog items -> research entries.
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "log.h"
#include "scan.h"
#include "texts.h"
#include "value.h"

namespace rd::extract {

struct Ingredient {
    Value count;     // ConstructionRecipe Count, or 1
    Value nameText;  // the ingredient item type's Name, a text value (null when it is not known)
    std::string en;  // its English and Russian name, or its asset name
    std::string ru;
};

// A catalog item: an item type with its own catalog data in one of the four printers. Text fields are
// empty when the item has no such text; the ...Text values are the texts to look up in other languages.
struct CatalogItem {
    std::string id;       // asset name, e.g. "Generator_Small"
    std::string package;  // e.g. "/Game/Items/ItemTypes/Components/Generator_Small"
    std::optional<std::string> classPath;
    std::optional<std::string> pickup;  // class path of the world actor the item prints as
    Value nameText;
    Value descriptionText;
    std::optional<std::string> en;
    std::optional<std::string> ru;
    std::optional<std::string> descRu;
    std::optional<std::string> descEn;
    std::string printer;  // printer key (taxonomy.h)
    Value seq;            // CategorySequenceNumber, or 0
    Value varSeq;         // VariationSequenceNumber, or 0
    Value cost;           // UnlockCost in bytes, 1000 when the asset leaves it out
    std::vector<Ingredient> recipe;
    std::optional<std::string> icon;      // icon texture name
    std::optional<std::string> iconPath;  // icon texture object path
    // How the item is unlocked: "research" (in the catalog from the start), "mission" (hidden until a
    // mission rewards it), "event" (hidden, bound to the player's PlayFab account) or "hidden".
    const char* unlock = "";
    // For a mission item, the title of the first mission that rewards it, in Russian (falling back to
    // English, then the mission id) and in English (falling back to Russian, then the mission id).
    std::string source;
    std::string sourceEn;
    Value sourceText;      // that mission's Title; null for other items
    const char* dlc = "";  // "Glitchwalkers" for the items of that expansion
};

// Every item type that has its own catalog data, a printer category (through its base items) and is
// not creative-only or a developer test, sorted by printer, then catalog position.
std::vector<CatalogItem> BuildCatalog(const GameAssets& assets, Localizer& texts, const Logger& log);

// A description as the overlay files hold it: without "\r", trimmed; empty for none.
std::string CleanDescription(const std::optional<std::string>& text);

// A research entry: a catalog item that kExclude (taxonomy.cpp) does not name.
struct ResearchItem {
    const CatalogItem* item;
    std::string description;    // descRu without "\r", trimmed
    std::string descriptionEn;  // descEn the same way
    const char* group;          // null when no rule matches
    const char* subgroup;
    const char* subgroupEn;
};

struct ResearchList {
    std::vector<ResearchItem> items;  // in catalog order
    // The icons the entries use and that could be decoded, in first-use order: the atlas order.
    std::vector<std::string> iconNames;
};

ResearchList BuildResearchList(const std::vector<CatalogItem>& catalog, const std::unordered_set<std::string>& decodedIcons,
                               const Logger& log);

}  // namespace rd::extract
