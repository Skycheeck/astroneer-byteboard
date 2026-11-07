#include "catalog.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "taxonomy.h"
#include "texts.h"
#include "uasset.h"

namespace rd::extract {

namespace {

// UnlockCost is left out of the asset when it equals the native ItemType default, which is 1000.
constexpr double kDefaultUnlockCost = 1000;
// Developer test assets that ship in the pak; one even has a printer category.
constexpr std::string_view kDevelopersDir = "/Game/Developers/";
// The Glitchwalkers expansion (paid DLC) keeps its content here.
constexpr std::string_view kGlitchwalkersDir = "/Game/U32_Expansion/";
constexpr const char* kGlitchwalkers = "Glitchwalkers";

// The effective properties of an item type: its parents' properties overridden by its own, and the
// same for CatalogData, as the game's class defaults inherit them. A property's effective value is
// its nearest definition up the parent chain.
class TypeResolver {
public:
    explicit TypeResolver(const std::vector<ItemType>& types) : types_(types), chains_(types.size()) {
        for (size_t k = 0; k < types.size(); ++k) {
            if (types[k].cls) indexByClass_[*types[k].cls] = k;  // a later type with the same class wins
        }
    }

    const ItemType& Get(size_t index) const { return types_[index]; }

    std::optional<size_t> IndexOfClass(const std::string& classPath) const {
        const auto found = indexByClass_.find(classPath);
        return found == indexByClass_.end() ? std::nullopt : std::optional<size_t>(found->second);
    }

    const Value* Prop(size_t index, std::string_view name) {
        for (size_t type : Chain(index)) {
            if (const Value* value = types_[type].props.Get(name)) return value;
        }
        return nullptr;
    }

    const Value* CatalogField(size_t index, std::string_view name) {
        for (size_t type : Chain(index)) {
            const Value* own = types_[type].props.Get("CatalogData");
            if (!own || own->type != Value::Type::Object) continue;
            if (const Value* value = own->Get(name)) return value;
        }
        return nullptr;
    }

private:
    // The type and its parents, nearest first. Throws when the inheritance loops.
    const std::vector<size_t>& Chain(size_t index) {
        if (!chains_[index]) {
            std::vector<size_t> chain{index};
            for (size_t current = index;;) {
                const std::optional<std::string>& sup = types_[current].sup;
                if (!sup || sup->empty()) break;
                const std::optional<size_t> parent = IndexOfClass(*sup);
                if (!parent) break;
                if (std::find(chain.begin(), chain.end(), *parent) != chain.end()) {
                    throw std::runtime_error("class inheritance loops through " + types_[*parent].package);
                }
                chain.push_back(*parent);
                current = *parent;
            }
            chains_[index] = std::move(chain);
        }
        return *chains_[index];
    }

    const std::vector<ItemType>& types_;
    std::unordered_map<std::string, size_t> indexByClass_;
    std::vector<std::optional<std::vector<size_t>>> chains_;
};

// What the catalog entries are built from.
struct Sources {
    TypeResolver& types;
    Localizer& texts;
    // Item class path -> the first mission that rewards it.
    const std::unordered_map<std::string, const Mission*>& rewardedBy;
    // Recipe ingredients whose item type is not in the pak, for one log line.
    std::vector<std::string>& unknownIngredients;
};

std::string IdOf(const std::string& package) {
    return package.substr(package.rfind('/') + 1);  // npos + 1 is 0
}

// The category reference; a variant without its own category takes its base item's.
const Value* CategoryOf(size_t index, TypeResolver& types) {
    std::vector<size_t> visited;
    for (;;) {
        const Value* category = types.CatalogField(index, "Category");
        if (Truthy(category)) return category;
        const Value* base = types.CatalogField(index, "BaseItemType");
        if (!Truthy(base) || std::find(visited.begin(), visited.end(), index) != visited.end()) return nullptr;
        visited.push_back(index);
        const std::optional<std::string> baseClass = RefPath(base);
        const std::optional<size_t> baseIndex = baseClass ? types.IndexOfClass(*baseClass) : std::nullopt;
        if (!baseIndex) return nullptr;
        index = *baseIndex;
    }
}

std::vector<Ingredient> RecipeOf(size_t index, Sources& sources) {
    const Value* recipe = sources.types.Prop(index, "ConstructionRecipe");
    const Value* ingredients = recipe ? recipe->Get("Ingredients") : nullptr;
    if (!Truthy(ingredients)) return {};
    if (ingredients->type != Value::Type::Array) {
        throw std::runtime_error("the recipe ingredients of " + sources.types.Get(index).package + " are not an array");
    }
    std::vector<Ingredient> result;
    for (const Value& ingredient : ingredients->array) {
        if (ingredient.type == Value::Type::Null) {
            throw std::runtime_error("a recipe ingredient of " + sources.types.Get(index).package + " is null");
        }
        const std::optional<std::string> classPath = RefPath(ingredient.Get("ItemType"));
        const std::string item = classPath ? ObjectName(*classPath) : std::string();
        const std::optional<size_t> itemIndex = classPath ? sources.types.IndexOfClass(*classPath) : std::nullopt;
        if (classPath && !itemIndex) sources.unknownIngredients.push_back(*classPath);
        const Value* displayName = itemIndex ? sources.types.Prop(*itemIndex, "Name") : nullptr;
        const Value* count = ingredient.Get("Count");
        Ingredient entry;
        entry.count = Truthy(count) ? *count : Value::Number(1);
        if (displayName) entry.nameText = *displayName;
        // The ingredient's asset name when it has no display name.
        const std::optional<std::string> en = sources.texts.Localize(displayName, kEnglish);
        entry.en = en && !en->empty() ? *en : item;
        const std::optional<std::string> ru = sources.texts.Localize(displayName, kRussian);
        entry.ru = ru && !ru->empty() ? *ru : item;
        result.push_back(std::move(entry));
    }
    return result;
}

// A mission's title in this language, else in the other one, else its id.
std::string MissionTitle(const Mission& mission, Localizer& texts, const std::string& locale) {
    const std::string& other = locale == kRussian ? kEnglish : kRussian;
    if (std::optional<std::string> title = texts.Translate(&mission.title, locale)) return *title;
    if (std::optional<std::string> title = texts.Translate(&mission.title, other)) return *title;
    return mission.id;
}

std::optional<CatalogItem> ToCatalogEntry(size_t index, Sources& sources) {
    TypeResolver& types = sources.types;
    const ItemType& type = types.Get(index);
    // Only item types that define their own catalog data are catalog entries; their children are not.
    if (!Truthy(type.props.Get("CatalogData"))) return std::nullopt;
    const std::string id = IdOf(type.package);
    if (type.package.starts_with(kDevelopersDir)) return std::nullopt;
    if (Truthy(types.CatalogField(index, "bIsCreativeModeOnly")) || IsCreativeOnly(id)) return std::nullopt;
    // "@/Game/Items/ItemTypes/CatalogCategories/ItemCategory_SmFab.ItemCategory_SmFab" -> SmFab; other
    // categories (Exploration, Discovery, the museum's shelves...) are not printed.
    const std::optional<std::string> categoryPath = RefPath(CategoryOf(index, types));
    const Printer* printer = categoryPath ? FindPrinter(ObjectName(*categoryPath)) : nullptr;
    if (!printer) return std::nullopt;

    CatalogItem item;
    item.id = id;
    item.package = type.package;
    item.classPath = type.cls;
    item.pickup = RefPath(types.Prop(index, "PickupActor"));
    if (const Value* name = types.Prop(index, "Name")) item.nameText = *name;
    if (const Value* description = types.Prop(index, "Description")) item.descriptionText = *description;
    item.en = sources.texts.Localize(&item.nameText, kEnglish);
    item.ru = sources.texts.Localize(&item.nameText, kRussian);
    item.descRu = sources.texts.Localize(&item.descriptionText, kRussian);
    item.descEn = sources.texts.Localize(&item.descriptionText, kEnglish);
    item.printer = printer->key;
    const Value* seq = types.CatalogField(index, "CategorySequenceNumber");
    item.seq = Truthy(seq) ? *seq : Value::Number(0);
    const Value* varSeq = types.CatalogField(index, "VariationSequenceNumber");
    item.varSeq = Truthy(varSeq) ? *varSeq : Value::Number(0);
    const Value* cost = types.CatalogField(index, "UnlockCost");
    item.cost = cost && cost->type != Value::Type::Null ? *cost : Value::Number(kDefaultUnlockCost);
    item.recipe = RecipeOf(index, sources);
    item.iconPath = RefPath(types.Prop(index, "WidgetIcon"));
    if (item.iconPath) item.icon = ObjectName(*item.iconPath);

    const auto mission = type.cls ? sources.rewardedBy.find(*type.cls) : sources.rewardedBy.end();
    if (!Truthy(types.CatalogField(index, "bHiddenUntilUnlocked"))) {
        item.unlock = "research";
    } else if (mission != sources.rewardedBy.end()) {
        item.unlock = "mission";
        item.sourceText = mission->second->title;
        item.source = MissionTitle(*mission->second, sources.texts, kRussian);
        item.sourceEn = MissionTitle(*mission->second, sources.texts, kEnglish);
    } else if (Truthy(types.CatalogField(index, "bPlayFabAccountBound"))) {
        item.unlock = "event";
    } else {
        item.unlock = "hidden";
    }
    if (type.package.starts_with(kGlitchwalkersDir)) item.dlc = kGlitchwalkers;
    return item;
}

// Printer, then position in the printer's catalog, then among the item's variants; negative when a
// goes first.
double ComparePosition(const CatalogItem& a, const CatalogItem& b) {
    if (a.printer != b.printer) return a.printer > b.printer ? 1 : -1;
    const double bySeq = ToJsNumber(&a.seq) - ToJsNumber(&b.seq);
    if (bySeq != 0 && !std::isnan(bySeq)) return bySeq;
    return ToJsNumber(&a.varSeq) - ToJsNumber(&b.varSeq);
}

// A stable merge sort. Unlike std::stable_sort it stays well-defined even for a comparator that is
// not a strict weak order (NaN positions in a malformed pak).
template <class Compare>
void StableSort(std::vector<size_t>& order, Compare compare) {
    std::vector<size_t> merged(order.size());
    for (size_t width = 1; width < order.size(); width *= 2) {
        for (size_t left = 0; left < order.size(); left += 2 * width) {
            const size_t middle = std::min(left + width, order.size());
            const size_t right = std::min(left + 2 * width, order.size());
            size_t i = left;
            size_t j = middle;
            size_t out = left;
            while (i < middle && j < right) merged[out++] = compare(order[j], order[i]) < 0 ? order[j++] : order[i++];
            while (i < middle) merged[out++] = order[i++];
            while (j < right) merged[out++] = order[j++];
        }
        order.swap(merged);
    }
}

}  // namespace

std::string CleanDescription(const std::optional<std::string>& text) {
    std::string description = text ? *text : std::string();
    std::erase(description, '\r');
    return std::string(JsTrim(description));
}

std::vector<CatalogItem> BuildCatalog(const GameAssets& assets, Localizer& texts, const Logger& log) {
    TypeResolver types(assets.itemTypes);
    std::unordered_map<std::string, const Mission*> rewardedBy;
    for (const Mission& mission : assets.missions) {
        for (const std::string& reward : mission.rewards) rewardedBy.try_emplace(reward, &mission);
    }
    std::vector<std::string> unknownIngredients;
    Sources sources{types, texts, rewardedBy, unknownIngredients};

    std::vector<CatalogItem> unsorted;
    for (size_t index = 0; index < assets.itemTypes.size(); ++index) {
        std::optional<CatalogItem> item = ToCatalogEntry(index, sources);
        if (item) unsorted.push_back(std::move(*item));
    }

    std::vector<size_t> order(unsorted.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    StableSort(order, [&](size_t a, size_t b) { return ComparePosition(unsorted[a], unsorted[b]); });
    std::vector<CatalogItem> items;
    items.reserve(unsorted.size());
    for (size_t k : order) items.push_back(std::move(unsorted[k]));

    texts.ReportMisses();
    if (!unknownIngredients.empty()) {
        std::sort(unknownIngredients.begin(), unknownIngredients.end());
        unknownIngredients.erase(std::unique(unknownIngredients.begin(), unknownIngredients.end()), unknownIngredients.end());
        std::string line = "recipe ingredients without an item type in the pak (their asset name is shown):";
        for (const std::string& classPath : unknownIngredients) line += " " + classPath;
        log(line);
    }
    log(std::to_string(items.size()) + " catalog items");
    return items;
}

ResearchList BuildResearchList(const std::vector<CatalogItem>& catalog, const std::unordered_set<std::string>& decodedIcons,
                               const Logger& log) {
    ResearchList list;
    std::unordered_map<std::string, std::string> firstBySignature;
    std::string unclassified;
    int withoutNameEn = 0;
    int withoutDescriptionEn = 0;
    for (const CatalogItem& item : catalog) {
        if (IsExcluded(item.id)) continue;

        // Two entries with the same name, cost and printer are most likely one item listed twice.
        const std::string signature = (item.ru ? *item.ru : "") + "|" + ToJsString(&item.cost) + "|" + item.printer;
        const auto [first, added] = firstBySignature.try_emplace(signature, item.id);
        if (!added) {
            log(first->second + " and " + item.id + " share name, cost and printer; "
                "if one is a duplicate, add it to kExclude in overlay/extract/taxonomy.cpp");
        }

        // An item without an English name is classified by its id; "null" matches no rule.
        const Classification classification = Classify(item.en ? *item.en : "null", item.id);
        if (!classification.group) unclassified += (unclassified.empty() ? "" : ", ") + item.id;
        ResearchItem entry{&item, CleanDescription(item.descRu), CleanDescription(item.descEn), classification.group,
                           classification.subgroup, classification.subgroupEn};
        if (!item.en || JsTrim(*item.en).empty()) ++withoutNameEn;
        if (entry.descriptionEn.empty()) ++withoutDescriptionEn;
        list.items.push_back(std::move(entry));
    }

    std::unordered_set<std::string> listed;
    for (const ResearchItem& entry : list.items) {
        const std::optional<std::string>& icon = entry.item->icon;
        if (icon && !icon->empty() && decodedIcons.contains(*icon) && listed.insert(*icon).second) {
            list.iconNames.push_back(*icon);
        }
    }

    log(std::to_string(list.items.size()) + " research items, " + std::to_string(list.iconNames.size()) + " icons");
    // The overlay shows the Russian text where the English one is missing.
    if (withoutNameEn || withoutDescriptionEn) {
        log(std::to_string(withoutNameEn) + " items have no English name and " + std::to_string(withoutDescriptionEn) +
            " no English description");
    }
    if (!unclassified.empty()) log("not in any group (add a rule to kRules in overlay/extract/taxonomy.cpp): " + unclassified);
    return list;
}

}  // namespace rd::extract
