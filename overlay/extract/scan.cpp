#include "scan.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>

#include "uasset.h"

namespace rd::extract {

namespace {

constexpr std::string_view kContentDir = "Astro/Content/";
constexpr std::string_view kItemTypesDir = "Astro/Content/Items/ItemTypes/";
constexpr std::string_view kAssetExtension = ".uasset";
constexpr std::string_view kCatalogDataClass = "ItemCatalogData";
constexpr std::string_view kMissionDataClass = "AstroMissionDataAsset";
constexpr size_t kMaxLinesShown = 20;

// A package that has an export of a class names the class in its name table, so a plain byte search
// finds the few packages worth parsing among the ~19,000 in the pak.
bool Mentions(const Bytes& uasset, std::string_view name) {
    return std::string_view(reinterpret_cast<const char*>(uasset.data()), uasset.size()).find(name) !=
           std::string_view::npos;
}

// "Astro/Content/X/Y.uasset" -> the .uexp next to it.
std::string UexpOf(const std::string& uassetFile) {
    return uassetFile.substr(0, uassetFile.size() - kAssetExtension.size()) + ".uexp";
}

// The class default object's properties, with the ItemCatalogData subobject's properties under CatalogData.
ItemType ToItemType(const std::string& package, const Asset& asset) {
    const Export* defaults = nullptr;
    for (const Export& e : asset.exports) {
        if (e.name.starts_with("Default__")) {
            defaults = &e;
            break;
        }
    }
    if (!defaults && !asset.exports.empty()) defaults = &asset.exports[0];
    const Export* catalogData = asset.FindByClass(kCatalogDataClass);
    const Export* blueprintClass = asset.FindByClass("BlueprintGeneratedClass");
    for (const Export* required : {defaults, catalogData}) {
        if (required && !required->propertiesEnd) throw FormatError("properties of " + required->name + " are unreadable");
    }
    if (!defaults) throw FormatError("the package has no exports");
    ItemType type;
    type.package = package;
    type.cls = defaults->classPath;
    type.sup = blueprintClass ? blueprintClass->superPath : std::nullopt;
    type.props = defaults->props;
    if (catalogData) type.props.Set("CatalogData", catalogData->props);
    return type;
}

// The MissionsData entries of a mission data asset that reward research.
std::vector<Mission> ReadMissions(const Asset& asset) {
    std::vector<Mission> missions;
    const Export* data = asset.FindByClass(kMissionDataClass);
    const Value* list = data ? data->props.Get("MissionsData") : nullptr;
    if (!list || list->type != Value::Type::Array) return missions;
    for (const Value& entry : list->array) {
        const Value* rewards = entry.Get("ResearchRewards");
        if (!rewards || rewards->type != Value::Type::Array) continue;
        Mission mission;
        const Value* id = entry.Get("MissionId");
        mission.id = id && id->type == Value::Type::String ? id->string : std::string();
        if (const Value* title = entry.Get("Title")) mission.title = *title;
        for (const Value& reward : rewards->array) {
            if (std::optional<std::string> classPath = RefPath(&reward)) mission.rewards.push_back(std::move(*classPath));
        }
        if (!mission.rewards.empty()) missions.push_back(std::move(mission));
    }
    return missions;
}

void LogList(const Logger& log, const std::vector<std::string>& lines) {
    for (size_t k = 0; k < std::min(lines.size(), kMaxLinesShown); ++k) log("  " + lines[k]);
    if (lines.size() > kMaxLinesShown) log("  … and " + std::to_string(lines.size() - kMaxLinesShown) + " more");
}

}  // namespace

GameAssets ReadGameAssets(const Pak& pak, const Logger& log) {
    GameAssets assets;
    std::vector<std::pair<std::string, std::vector<Mission>>> missionFiles;
    std::vector<std::string> failures;
    std::vector<std::string> warnings;
    std::unordered_set<std::string> classes;  // of the item types read so far

    auto addItemType = [&](const std::string& package, const Asset& asset) {
        ItemType type = ToItemType(package, asset);
        if (type.cls) classes.insert(*type.cls);
        assets.itemTypes.push_back(std::move(type));
        for (const std::string& warning : asset.warnings) warnings.push_back(package + ": " + warning);
    };

    for (const std::string& file : pak.Files()) {
        // Game content only; the engine's own packages (Engine/...) have no /Game/ path.
        if (!file.starts_with(kContentDir) || !file.ends_with(kAssetExtension)) continue;
        const std::string package = PakPathToPackage(file);
        // Everything under ItemTypes/ is an item type; elsewhere a package that may hold one fails the
        // extraction when it cannot be read, while a mission package only loses its missions.
        bool itemCandidate = file.starts_with(kItemTypesDir);
        try {
            const Bytes uasset = pak.Read(file);
            itemCandidate = itemCandidate || Mentions(uasset, kCatalogDataClass);
            const bool missionData = Mentions(uasset, kMissionDataClass);
            if (!itemCandidate && !missionData) continue;
            const Asset asset = ParseAsset(uasset, pak.Read(UexpOf(file)), package);
            if (file.starts_with(kItemTypesDir) || asset.FindByClass(kCatalogDataClass)) addItemType(package, asset);
            if (missionData) {
                std::vector<Mission> missions = ReadMissions(asset);
                if (!missions.empty()) missionFiles.push_back({file, std::move(missions)});
            }
        } catch (const std::exception& error) {
            if (itemCandidate) {
                failures.push_back(package + ": " + error.what());
            } else {
                log("missions of " + package + " could not be read: " + error.what());
            }
        }
    }

    // Parent classes outside ItemTypes/ without catalog data of their own (FaultFinder_Align_IT, which
    // holds the Fault Finder's name): loaded by package path, each package at most once, so the loop
    // ends after at most one round per package.
    std::unordered_set<std::string> triedPackages;
    const size_t scanned = assets.itemTypes.size();
    for (size_t k = 0; k < assets.itemTypes.size(); ++k) {
        const std::optional<std::string> sup = assets.itemTypes[k].sup;  // a copy: the vector grows below
        if (!sup || !sup->starts_with("/Game/") || classes.contains(*sup)) continue;
        const std::string package = PackageOf(*sup);
        if (!triedPackages.insert(package).second) continue;
        const std::string file = PackageToPakPath(package) + std::string(kAssetExtension);
        if (!pak.Contains(file)) {
            log("parent class " + *sup + " is not in the pak");
            continue;
        }
        try {
            const Asset asset = ParseAsset(pak.Read(file), pak.Read(UexpOf(file)), package);
            addItemType(package, asset);
        } catch (const std::exception& error) {
            failures.push_back(package + ": " + error.what());
        }
    }

    if (!warnings.empty()) {
        log(std::to_string(warnings.size()) + " properties could not be read and are left out:");
        LogList(log, warnings);
    }
    if (!failures.empty()) {
        std::string message = std::to_string(failures.size()) + " item types could not be read:";
        for (size_t k = 0; k < std::min(failures.size(), kMaxLinesShown); ++k) message += "\n  " + failures[k];
        if (failures.size() > kMaxLinesShown) message += "\n  … and " + std::to_string(failures.size() - kMaxLinesShown) + " more";
        throw ToolError(message);
    }

    std::sort(missionFiles.begin(), missionFiles.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [file, missions] : missionFiles) {
        for (Mission& mission : missions) assets.missions.push_back(std::move(mission));
    }
    log(std::to_string(assets.itemTypes.size()) + " item types (" + std::to_string(assets.itemTypes.size() - scanned) +
        " of them parent classes loaded by path), " + std::to_string(assets.missions.size()) +
        " missions that reward research");
    return assets;
}

}  // namespace rd::extract
