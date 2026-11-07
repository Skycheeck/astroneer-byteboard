#pragma once
// The item catalog and its taxonomy (purpose groups and printers), read from <mod>/data/catalog.tsv
// and <mod>/data/taxonomy.tsv. The DLL extracts both from the game (gamedata.cpp); the format is in docs/PROTOCOL.md.
// Threads: LoadCatalog runs once on the overlay's MainThread before the hooks are installed. The
// data is read-only afterwards, so every thread reads it without locking.
#include <string>
#include <unordered_map>
#include <vector>

namespace rd {

struct Group {
    std::string key;      // "power", "oxygen", ...
    std::string label;    // "Энергия"
    std::string labelEn;  // "Power"; empty in older data
};

struct Printer {
    std::string key;         // "Backpack", "SmFab", "MedFab", "LgFab"
    std::string category;    // "ItemCategory_SmFab", as the mod reports an open printer
    std::string label;         // "Малый принтер"
    std::string shortLabel;    // "малый"
    std::string labelEn;       // "Small Printer"; empty in older data
    std::string shortLabelEn;  // "small"
};

struct Ingredient {
    int count = 0;
    std::string name;
};

// How an item becomes researched (catalog.tsv "unlock"). Only Research items can be bought for bytes;
// the others are hidden in the game's catalog until a mission, an event or something else unlocks them.
enum class Unlock { Research, Mission, Event, Hidden };

// An item's game texts in one more language (data/texts.tsv).
struct LocalTexts {
    std::string name;
    std::string description;
    std::string recipe;
    std::string source;
    std::vector<Ingredient> ingredients;
};

struct Item {
    std::string id;
    std::string classPath;
    std::string nameRu;
    std::string nameEn;
    std::string subgroup;
    std::string subgroupEn;
    std::string recipe;  // "2× Смола, 1× Медь"
    std::string recipeEn;
    std::string description;
    std::string descriptionEn;
    std::string pickup;  // class path of the world actor class the item prints as
    long long cost = 0;
    Unlock unlock = Unlock::Research;
    std::string source;     // for Unlock::Mission: the title of the mission that gives the item
    std::string sourceEn;
    std::string dlc;        // the expansion the item belongs to ("Glitchwalkers"), or empty
    int icon = -1;          // index into icons.rgba; -1 for none
    int groupIndex = 0;     // into Groups()
    int printerIndex = 0;   // into Printers()
    std::string classKey;   // ClassKey(classPath): matched against the unlocked list
    std::string pickupKey;  // ClassKey(pickup): matched against the printer's current recipe
    std::vector<Ingredient> ingredients;
    std::vector<Ingredient> ingredientsEn;  // empty in older data
    std::unordered_map<std::string, LocalTexts> texts;  // locale ("de", "pt-BR") -> texts, from data/texts.tsv
    std::wstring searchText;  // Lower(): names, group, subgroup, printer and recipe
};

// False when catalog.tsv cannot be read or its header lacks a required column. A group or printer
// key that taxonomy.tsv does not list puts the item into a fallback group or printer (and is
// logged), so no item is dropped.
bool LoadCatalog();

// The text in the interface language (lang.h); English falls back to Russian where data lacks it.
const std::string& NameOf(const Item& item);
const std::string& SubgroupOf(const Item& item);
const std::string& DescriptionOf(const Item& item);
const std::string& SourceOf(const Item& item);
const std::string& RecipeOf(const Item& item);
const std::vector<Ingredient>& IngredientsOf(const Item& item);
const std::string& LabelOf(const Group& group);
const std::string& LabelOf(const Printer& printer);
const std::string& ShortLabelOf(const Printer& printer);

// The locales data/texts.tsv has texts for (besides Russian and English).
const std::vector<std::string>& DataLocales();
const std::vector<Item>& Items();  // within a subgroup: research items cheapest first, then rewards; then by printer order
const std::vector<Group>& Groups();  // display order
const std::vector<Printer>& Printers();  // display order
const Item* FindItem(const std::string& id);
// Index into Printers() of the printer with this ItemCategory_..., or -1.
int FindPrinterByCategory(const std::string& category);

}  // namespace rd
