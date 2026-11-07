#pragma once
// Finds the game's item types and missions in the pak.
#include <optional>
#include <string>
#include <vector>

#include "log.h"
#include "pak.h"
#include "value.h"

namespace rd::extract {

// An item type blueprint.
struct ItemType {
    std::string package;             // e.g. "/Game/Items/ItemTypes/Components/Generator_Small"
    std::optional<std::string> cls;  // its class path, e.g. ".../Generator_Small.Generator_Small_C"
    std::optional<std::string> sup;  // its parent class path
    // The class default object's tagged properties (encoding in uasset.h), with the ItemCatalogData
    // subobject's properties under CatalogData.
    Value props;
};

// An entry of a mission data asset (AstroMissionDataAsset MissionsData) that rewards research.
struct Mission {
    std::string id;                    // MissionId
    Value title;                       // Title, a text value
    std::vector<std::string> rewards;  // ResearchRewards: the class paths of the items it unlocks
};

struct GameAssets {
    // In pak order: every package under /Game/Items/ItemTypes/ and every other package with an
    // ItemCatalogData export; then the parent classes of those that were not among them.
    std::vector<ItemType> itemTypes;
    // Ordered by package path, then by their order in the package.
    std::vector<Mission> missions;
};

// Reads every .uasset in the pak once; only packages that mention ItemCatalogData or
// AstroMissionDataAsset are parsed. Throws ToolError when an item type cannot be read, so a parse error
// never turns into a shorter catalog.
GameAssets ReadGameAssets(const Pak& pak, const Logger& log);

}  // namespace rd::extract
