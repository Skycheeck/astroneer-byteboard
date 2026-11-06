#pragma once
// The research catalog, built the way tools/steps/dump-itemtypes.mjs, build-catalog.mjs and
// build-items.mjs build it: item type blueprints -> catalog items -> research entries.
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "log.h"
#include "pak.h"
#include "value.h"

namespace rd::extract {

// An item type blueprint (dump-itemtypes).
struct ItemType {
    std::string key;                 // path under /Game/Items/ItemTypes/, e.g. "Components/Generator_Small"
    std::optional<std::string> cls;  // its class path
    std::optional<std::string> sup;  // its parent class path
    Value props;                     // the class defaults' properties, CatalogData holding the catalog data's
};

// Every item type blueprint in the pak, in the order the Node step lists them. Throws ToolError when
// one cannot be read, so a parse error never turns into a shorter catalog.
std::vector<ItemType> ReadItemTypes(const Pak& pak, const Logger& log);

struct Ingredient {
    Value count;  // ConstructionRecipe Count, or 1
    std::string en;
    std::string ru;
};

// A catalog item (build-catalog), with the fields the overlay files need. Text fields are empty where
// the Node step has null.
struct CatalogItem {
    std::string id;
    std::optional<std::string> classPath;
    std::optional<std::string> pickup;
    std::optional<std::string> en;
    std::optional<std::string> ru;
    std::optional<std::string> descRu;
    std::string printer;
    Value seq;     // CategorySequenceNumber || 0
    Value varSeq;  // VariationSequenceNumber || 0
    Value cost;    // UnlockCost ?? 1000
    std::vector<Ingredient> recipe;
    std::optional<std::string> icon;
    std::optional<std::string> iconPath;
};

// Every item type that has catalog data, a printer category and is not creative-only, sorted by
// printer, then catalog position.
std::vector<CatalogItem> BuildCatalog(const Pak& pak, const std::vector<ItemType>& itemTypes, const Logger& log);

// A research entry (build-items): a catalog item below the not-researchable cost that EXCLUDE does not name.
struct ResearchItem {
    const CatalogItem* item;
    std::string description;  // descRu without "\r", trimmed
    const char* group;        // null when no rule matches
    const char* subgroup;
};

struct ResearchList {
    std::vector<ResearchItem> items;  // in catalog order
    // The icons the entries use and that could be decoded, in the order of items.json's icons object.
    std::vector<std::string> iconNames;
};

ResearchList BuildResearchList(const std::vector<CatalogItem>& catalog, const std::unordered_set<std::string>& decodedIcons,
                               const Logger& log);

}  // namespace rd::extract
