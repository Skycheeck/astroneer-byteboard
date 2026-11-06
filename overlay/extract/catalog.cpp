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

constexpr std::string_view kItemTypesDir = "Astro/Content/Items/ItemTypes/";
constexpr std::string_view kAssetExtension = ".uasset";
constexpr size_t kMaxLinesShown = 20;
// UnlockCost is left out of the asset when it equals the native ItemType default, which is 1000.
constexpr double kDefaultUnlockCost = 1000;

void LogList(const Logger& log, const std::vector<std::string>& lines) {
    for (size_t k = 0; k < std::min(lines.size(), kMaxLinesShown); ++k) log("  " + lines[k]);
    if (lines.size() > kMaxLinesShown) log("  … and " + std::to_string(lines.size() - kMaxLinesShown) + " more");
}

// The class defaults object's properties, with the ItemCatalogData subobject's properties under CatalogData.
ItemType ToItemType(const std::string& key, const Asset& asset) {
    const Export* defaults = nullptr;
    for (const Export& e : asset.exports) {
        if (e.name.starts_with("Default__")) {
            defaults = &e;
            break;
        }
    }
    if (!defaults && !asset.exports.empty()) defaults = &asset.exports[0];
    const Export* catalogData = asset.FindByClass("ItemCatalogData");
    const Export* blueprintClass = asset.FindByClass("BlueprintGeneratedClass");
    for (const Export* required : {defaults, catalogData}) {
        if (required && !required->propertiesEnd) throw FormatError("properties of " + required->name + " are unreadable");
    }
    if (!defaults) throw FormatError("the package has no exports");
    ItemType type;
    type.key = key;
    type.cls = defaults->classPath;
    type.sup = blueprintClass ? blueprintClass->superPath : std::nullopt;
    type.props = defaults->props;
    if (catalogData) type.props.Set("CatalogData", catalogData->props);
    return type;
}

// The effective properties of an item type: its parents' properties overridden by its own, and the
// same for CatalogData, as the game's class defaults inherit them. A property's effective value is
// its nearest definition up the parent chain.
class TypeResolver {
public:
    explicit TypeResolver(const std::vector<ItemType>& types) : types_(types), chains_(types.size()) {
        for (size_t k = 0; k < types.size(); ++k) {
            if (types[k].cls) keyByClass_[*types[k].cls] = k;  // a later type with the same class wins
        }
    }

    const ItemType& Get(size_t index) const { return types_[index]; }

    std::optional<size_t> IndexOfClass(const std::string& classPath) const {
        const auto found = keyByClass_.find(classPath);
        return found == keyByClass_.end() ? std::nullopt : std::optional<size_t>(found->second);
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
    // The type and its parents, nearest first. Throws when the inheritance loops, as resolve() does.
    const std::vector<size_t>& Chain(size_t index) {
        if (!chains_[index]) {
            std::vector<size_t> chain{index};
            for (size_t current = index;;) {
                const std::optional<std::string>& sup = types_[current].sup;
                if (!sup || sup->empty()) break;
                const std::optional<size_t> parent = IndexOfClass(*sup);
                if (!parent) break;
                if (std::find(chain.begin(), chain.end(), *parent) != chain.end()) {
                    throw std::runtime_error("class inheritance loops through " + types_[*parent].key);
                }
                chain.push_back(*parent);
                current = *parent;
            }
            chains_[index] = std::move(chain);
        }
        return *chains_[index];
    }

    const std::vector<ItemType>& types_;
    std::unordered_map<std::string, size_t> keyByClass_;
    std::vector<std::optional<std::vector<size_t>>> chains_;
};

std::string IdOf(const std::string& key) {
    return key.substr(key.rfind('/') + 1);  // npos + 1 is 0
}

// The printer category reference; a variant without its own category takes its base item's.
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

// "@/Game/Items/ItemCategories/ItemCategory_SmFab.ItemCategory_SmFab" -> "SmFab"
std::string PrinterOf(const Value& category) {
    const std::optional<std::string> path = RefPath(&category);
    if (!path) return ToJsString(&category);
    std::string name = ObjectName(*path);
    constexpr std::string_view kPrefix = "ItemCategory_";
    if (name.starts_with(kPrefix)) name.erase(0, kPrefix.size());
    return name;
}

std::vector<Ingredient> RecipeOf(size_t index, TypeResolver& types, Localizer& texts) {
    const Value* recipe = types.Prop(index, "ConstructionRecipe");
    const Value* ingredients = recipe ? recipe->Get("Ingredients") : nullptr;
    if (!Truthy(ingredients)) return {};
    // Where Node's .map() would throw a TypeError.
    if (ingredients->type != Value::Type::Array) {
        throw std::runtime_error("the recipe ingredients of " + types.Get(index).key + " are not an array");
    }
    std::vector<Ingredient> result;
    for (const Value& ingredient : ingredients->array) {
        if (ingredient.type == Value::Type::Null) {
            throw std::runtime_error("a recipe ingredient of " + types.Get(index).key + " is null");
        }
        const std::optional<std::string> classPath = RefPath(ingredient.Get("ItemType"));
        const std::string item = classPath ? ObjectName(*classPath) : std::string();
        const std::optional<size_t> itemIndex = classPath ? types.IndexOfClass(*classPath) : std::nullopt;
        const Value* displayName = itemIndex ? types.Prop(*itemIndex, "Name") : nullptr;
        const Value* count = ingredient.Get("Count");
        Ingredient entry;
        entry.count = Truthy(count) ? *count : Value::Number(1);
        const std::optional<std::string> en = texts.Localize(displayName, Language::En);
        entry.en = en && !en->empty() ? *en : item;
        const std::optional<std::string> ru = texts.Localize(displayName, Language::Ru);
        entry.ru = ru && !ru->empty() ? *ru : item;
        result.push_back(std::move(entry));
    }
    return result;
}

std::optional<CatalogItem> ToCatalogEntry(size_t index, TypeResolver& types, Localizer& texts) {
    const ItemType& type = types.Get(index);
    // Only item types that define their own catalog data are catalog entries; their children are not.
    if (!Truthy(type.props.Get("CatalogData"))) return std::nullopt;
    const std::string id = IdOf(type.key);
    if (Truthy(types.CatalogField(index, "bIsCreativeModeOnly")) || IsCreativeOnly(id)) return std::nullopt;
    const Value* category = CategoryOf(index, types);
    if (!category) return std::nullopt;

    CatalogItem item;
    item.id = id;
    item.classPath = type.cls;
    item.pickup = RefPath(types.Prop(index, "PickupActor"));
    item.en = texts.Localize(types.Prop(index, "Name"), Language::En);
    item.ru = texts.Localize(types.Prop(index, "Name"), Language::Ru);
    item.descRu = texts.Localize(types.Prop(index, "Description"), Language::Ru);
    item.printer = PrinterOf(*category);
    const Value* seq = types.CatalogField(index, "CategorySequenceNumber");
    item.seq = Truthy(seq) ? *seq : Value::Number(0);
    const Value* varSeq = types.CatalogField(index, "VariationSequenceNumber");
    item.varSeq = Truthy(varSeq) ? *varSeq : Value::Number(0);
    const Value* cost = types.CatalogField(index, "UnlockCost");
    item.cost = cost && cost->type != Value::Type::Null ? *cost : Value::Number(kDefaultUnlockCost);
    item.recipe = RecipeOf(index, types, texts);
    item.iconPath = RefPath(types.Prop(index, "WidgetIcon"));
    if (item.iconPath) item.icon = ObjectName(*item.iconPath);
    return item;
}

// byPrinterThenPosition: negative when a goes first.
double ComparePosition(const CatalogItem& a, const CatalogItem& b) {
    if (a.printer != b.printer) return JsStringLess(b.printer, a.printer) ? 1 : -1;
    const double bySeq = ToJsNumber(&a.seq) - ToJsNumber(&b.seq);
    if (bySeq != 0 && !std::isnan(bySeq)) return bySeq;
    return ToJsNumber(&a.varSeq) - ToJsNumber(&b.varSeq);
}

// A stable merge sort. Unlike std::stable_sort it stays well-defined for any comparator, and for a
// consistent one it gives the same order as JavaScript's (also stable) Array.prototype.sort.
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

std::vector<ItemType> ReadItemTypes(const Pak& pak, const Logger& log) {
    std::vector<ItemType> itemTypes;
    std::vector<std::string> failures;
    std::vector<std::string> warnings;
    for (const std::string& file : pak.Files()) {
        if (!file.starts_with(kItemTypesDir) || !file.ends_with(kAssetExtension)) continue;
        const std::string base = file.substr(0, file.size() - kAssetExtension.size());
        const std::string key = base.substr(kItemTypesDir.size());
        try {
            const Bytes uasset = pak.Read(file);
            const Bytes uexp = pak.Read(base + ".uexp");
            const Asset asset = ParseAsset(uasset, uexp, PakPathToPackage(file));
            itemTypes.push_back(ToItemType(key, asset));
            for (const std::string& warning : asset.warnings) warnings.push_back(key + ": " + warning);
        } catch (const std::exception& error) {
            failures.push_back(key + ": " + error.what());
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

    // The Node step keeps them in a JavaScript object, which lists array-index keys first.
    std::vector<std::string> keys;
    for (const ItemType& type : itemTypes) keys.push_back(type.key);
    std::vector<ItemType> ordered;
    ordered.reserve(itemTypes.size());
    for (size_t k : JsKeyOrder(keys)) ordered.push_back(std::move(itemTypes[k]));
    log(std::to_string(ordered.size()) + " item types");
    return ordered;
}

std::vector<CatalogItem> BuildCatalog(const Pak& pak, const std::vector<ItemType>& itemTypes, const Logger& log) {
    TypeResolver types(itemTypes);
    Localizer texts(pak, log);
    std::vector<CatalogItem> unsorted;
    for (size_t index = 0; index < itemTypes.size(); ++index) {
        std::optional<CatalogItem> item = ToCatalogEntry(index, types, texts);
        if (item) unsorted.push_back(std::move(*item));
    }

    std::vector<size_t> order(unsorted.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    StableSort(order, [&](size_t a, size_t b) { return ComparePosition(unsorted[a], unsorted[b]); });
    std::vector<CatalogItem> items;
    items.reserve(unsorted.size());
    for (size_t k : order) items.push_back(std::move(unsorted[k]));

    texts.ReportMisses();
    log(std::to_string(items.size()) + " catalog items");
    return items;
}

ResearchList BuildResearchList(const std::vector<CatalogItem>& catalog, const std::unordered_set<std::string>& decodedIcons,
                               const Logger& log) {
    ResearchList list;
    std::unordered_map<std::string, std::string> firstBySignature;
    std::string unclassified;
    for (const CatalogItem& item : catalog) {
        if (!(ToJsNumber(&item.cost) < kNotResearchableCost) || IsExcluded(item.id)) continue;

        // Two entries with the same name, cost and printer are most likely one item listed twice.
        const std::string signature = (item.ru ? *item.ru : "null") + "|" + ToJsString(&item.cost) + "|" + item.printer;
        const auto [first, added] = firstBySignature.try_emplace(signature, item.id);
        if (!added) {
            log(first->second + " and " + item.id + " share name, cost and printer; "
                "if one is a duplicate, add it to EXCLUDE in tools/lib/taxonomy.mjs");
        }

        // RegExp.test(null) tests the string "null".
        const Classification classification = Classify(item.en ? *item.en : "null", item.id);
        if (!classification.group) unclassified += (unclassified.empty() ? "" : ", ") + item.id;
        std::string description = item.descRu ? *item.descRu : std::string();
        std::erase(description, '\r');
        list.items.push_back({&item, std::string(JsTrim(description)), classification.group, classification.subgroup});
    }

    std::vector<std::string> iconNames;
    std::unordered_set<std::string> listed;
    for (const ResearchItem& entry : list.items) {
        const std::optional<std::string>& icon = entry.item->icon;
        if (icon && !icon->empty() && decodedIcons.contains(*icon) && listed.insert(*icon).second) iconNames.push_back(*icon);
    }
    // items.json keeps them in a JavaScript object, which lists array-index keys first.
    for (size_t k : JsKeyOrder(iconNames)) list.iconNames.push_back(iconNames[k]);

    log(std::to_string(list.items.size()) + " research items, " + std::to_string(list.iconNames.size()) + " icons");
    if (!unclassified.empty()) log("not in any group (add a rule in tools/lib/taxonomy.mjs): " + unclassified);
    return list;
}

}  // namespace rd::extract
