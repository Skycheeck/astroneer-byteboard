#include "catalog.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string_view>
#include <unordered_map>

#include "paths.h"
#include "util.h"

namespace rd {

namespace {

// The columns of catalog.tsv, in the order docs/PROTOCOL.md lists them. The '#' header row names
// the columns, so the reader finds each one by name.
enum Column { Id, ClassPath, NameRu, NameEn, GroupKey, Subgroup, PrinterKey, Cost, Recipe, Icon, Description, Pickup, ColumnCount };
constexpr const char* kColumnNames[ColumnCount] = {"id",   "classPath", "nameRu", "nameEn", "group",       "subgroup",
                                                   "printer", "cost",   "recipe", "icon",   "description", "pickup"};
constexpr Column kRequiredColumns[] = {Id, ClassPath, NameRu, GroupKey, PrinterKey, Cost};

// Where items with a key that taxonomy.tsv does not list are shown.
constexpr const char* kFallbackGroupLabel = "Прочее";
constexpr const char* kFallbackPrinterLabel = "Другой принтер";
constexpr const char* kFallbackPrinterShort = "другой";

struct Catalog {
    std::vector<Item> items;
    std::vector<Group> groups;
    std::vector<Printer> printers;
    std::unordered_map<std::string, size_t> indexById;
};
Catalog g_catalog;

std::string_view WithoutCr(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

void LoadTaxonomy(std::vector<Group>& groups, std::vector<Printer>& printers) {
    std::string text;
    if (!ReadSharedFile(Paths().overlay / "taxonomy.tsv", text)) {
        Log("taxonomy.tsv not found: all items are shown under the fallback group and printer");
        return;
    }
    for (std::string_view line : Split(text, '\n')) {
        line = WithoutCr(line);
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string_view> cells = Split(line, '\t');
        if (cells[0] == "group" && cells.size() >= 3) {
            groups.push_back({std::string(cells[1]), std::string(cells[2])});
        } else if (cells[0] == "printer" && cells.size() >= 5) {
            printers.push_back({std::string(cells[1]), std::string(cells[2]), std::string(cells[3]), std::string(cells[4])});
        } else {
            Log("taxonomy.tsv: ignored line: " + std::string(line));
        }
    }
}

// Column positions from the header row ("#id<TAB>classPath<TAB>...").
bool MapColumns(std::string_view header, int (&position)[ColumnCount]) {
    std::fill(std::begin(position), std::end(position), -1);
    const std::vector<std::string_view> names = Split(header.substr(1), '\t');
    for (size_t i = 0; i < names.size(); ++i) {
        for (int c = 0; c < ColumnCount; ++c) {
            if (Trim(names[i]) == kColumnNames[c]) position[c] = (int)i;
        }
    }
    for (Column c : kRequiredColumns) {
        if (position[c] < 0) {
            Log(std::string("catalog.tsv: the header row has no column '") + kColumnNames[c] + "'");
            return false;
        }
    }
    return true;
}

// "2× Смола, 1× Медь" -> {2, "Смола"}, {1, "Медь"}
std::vector<Ingredient> ParseRecipe(std::string_view recipe) {
    std::vector<Ingredient> out;
    for (std::string_view part : Split(recipe, ',')) {
        part = Trim(part);
        const size_t space = part.find(' ');
        if (part.empty() || space == std::string_view::npos) continue;
        out.push_back({std::atoi(std::string(part).c_str()), std::string(part.substr(space + 1))});
    }
    return out;
}

// Index of the group or printer with this key; unknown keys get one fallback entry, appended
// after the listed ones, and are remembered for the log.
class KeyResolver {
public:
    template <class T>
    int Resolve(const std::string& key, std::vector<T>& entries, const T& fallback) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].key.empty() && entries[i].key == key) return (int)i;
        }
        unknownKeys_.insert(key.empty() ? "(empty)" : key);
        if (fallbackIndex_ < 0) {
            fallbackIndex_ = (int)entries.size();
            entries.push_back(fallback);
        }
        return fallbackIndex_;
    }

    void LogUnknown(const char* what, const char* shownAs) const {
        if (unknownKeys_.empty()) return;
        std::string keys;
        for (const std::string& k : unknownKeys_) keys += (keys.empty() ? "" : ", ") + k;
        Log(std::string("catalog: unknown ") + what + " keys, shown as «" + shownAs + "»: " + keys);
    }

private:
    int fallbackIndex_ = -1;
    std::set<std::string> unknownKeys_;
};

}  // namespace

bool LoadCatalog() {
    std::string text;
    if (!ReadSharedFile(Paths().overlay / "catalog.tsv", text)) {
        Log("catalog.tsv cannot be read");
        return false;
    }
    Catalog& cat = g_catalog;
    LoadTaxonomy(cat.groups, cat.printers);

    int position[ColumnCount];
    for (int c = 0; c < ColumnCount; ++c) position[c] = c;
    int requiredCells = 0;
    for (Column c : kRequiredColumns) requiredCells = std::max(requiredCells, position[c] + 1);

    KeyResolver groups, printers;
    int skipped = 0;
    bool firstLine = true;
    for (std::string_view line : Split(text, '\n')) {
        line = WithoutCr(line);
        const bool header = firstLine && !line.empty() && line.front() == '#';
        firstLine = false;
        if (header) {
            if (!MapColumns(line, position)) return false;
            requiredCells = 0;
            for (Column c : kRequiredColumns) requiredCells = std::max(requiredCells, position[c] + 1);
            continue;
        }
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string_view> cells = Split(line, '\t');
        if ((int)cells.size() < requiredCells) {
            ++skipped;
            continue;
        }
        const auto cell = [&](Column c) {
            const int at = position[c];
            return at >= 0 && at < (int)cells.size() ? std::string(cells[at]) : std::string();
        };
        Item it;
        it.id = cell(Id);
        it.classPath = cell(ClassPath);
        it.nameRu = cell(NameRu);
        it.nameEn = cell(NameEn);
        it.subgroup = cell(Subgroup);
        it.recipe = cell(Recipe);
        it.description = cell(Description);
        it.pickup = cell(Pickup);
        it.cost = std::atoll(cell(Cost).c_str());
        const std::string icon = cell(Icon);
        it.icon = icon.empty() ? -1 : std::atoi(icon.c_str());
        it.groupIndex = groups.Resolve(cell(GroupKey), cat.groups, Group{"", kFallbackGroupLabel});
        it.printerIndex = printers.Resolve(cell(PrinterKey), cat.printers, Printer{"", "", kFallbackPrinterLabel, kFallbackPrinterShort});
        it.classKey = ClassKey(it.classPath);
        it.pickupKey = ClassKey(it.pickup);
        it.ingredients = ParseRecipe(it.recipe);
        cat.items.push_back(std::move(it));
    }
    for (Item& it : cat.items) {
        const std::string searchable = it.nameRu + " " + it.nameEn + " " + cat.groups[it.groupIndex].label + " " + it.subgroup + " " +
                                       cat.printers[it.printerIndex].label + " " + it.recipe;
        it.searchText = Lower(Widen(searchable));
    }
    // Within a subgroup, cheapest first: the progression across printers.
    std::stable_sort(cat.items.begin(), cat.items.end(), [](const Item& a, const Item& b) {
        return a.cost != b.cost ? a.cost < b.cost : a.printerIndex < b.printerIndex;
    });
    for (size_t i = 0; i < cat.items.size(); ++i) cat.indexById.emplace(cat.items[i].id, i);

    Log("catalog: " + std::to_string(cat.items.size()) + " items, " + std::to_string(cat.groups.size()) + " groups, " +
        std::to_string(cat.printers.size()) + " printers");
    if (skipped) Log("catalog: skipped " + std::to_string(skipped) + " lines with too few columns");
    groups.LogUnknown("group", kFallbackGroupLabel);
    printers.LogUnknown("printer", kFallbackPrinterLabel);
    return true;
}

const std::vector<Item>& Items() {
    return g_catalog.items;
}

const std::vector<Group>& Groups() {
    return g_catalog.groups;
}

const std::vector<Printer>& Printers() {
    return g_catalog.printers;
}

const Item* FindItem(const std::string& id) {
    const auto found = g_catalog.indexById.find(id);
    return found == g_catalog.indexById.end() ? nullptr : &g_catalog.items[found->second];
}

int FindPrinterByCategory(const std::string& category) {
    for (size_t i = 0; i < g_catalog.printers.size(); ++i) {
        const Printer& p = g_catalog.printers[i];
        if (!p.category.empty() && p.category == category) return (int)i;
    }
    return -1;
}

}  // namespace rd
