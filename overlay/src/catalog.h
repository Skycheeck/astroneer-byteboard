#pragma once
// The item catalog and its taxonomy (purpose groups and printers), read from <mod>/data/catalog.tsv
// and <mod>/data/taxonomy.tsv. The DLL extracts both from the game (gamedata.cpp); the format is in docs/PROTOCOL.md.
// Threads: LoadCatalog runs once on the overlay's MainThread before the hooks are installed. The
// data is read-only afterwards, so every thread reads it without locking.
#include <string>
#include <vector>

namespace rd {

struct Group {
    std::string key;    // "power", "oxygen", ...
    std::string label;  // "Энергия"
};

struct Printer {
    std::string key;         // "Backpack", "SmFab", "MedFab", "LgFab"
    std::string category;    // "ItemCategory_SmFab", as the mod reports an open printer
    std::string label;       // "Малый принтер"
    std::string shortLabel;  // "малый"
};

struct Ingredient {
    int count = 0;
    std::string name;
};

struct Item {
    std::string id;
    std::string classPath;
    std::string nameRu;
    std::string nameEn;
    std::string subgroup;
    std::string recipe;  // "2× Смола, 1× Медь"
    std::string description;
    std::string pickup;  // class path of the world actor class the item prints as
    long long cost = 0;
    int icon = -1;          // index into icons.rgba; -1 for none
    int groupIndex = 0;     // into Groups()
    int printerIndex = 0;   // into Printers()
    std::string classKey;   // ClassKey(classPath): matched against the unlocked list
    std::string pickupKey;  // ClassKey(pickup): matched against the printer's current recipe
    std::vector<Ingredient> ingredients;
    std::wstring searchText;  // Lower(): names, group, subgroup, printer and recipe
};

// False when catalog.tsv cannot be read or its header lacks a required column. A group or printer
// key that taxonomy.tsv does not list puts the item into a fallback group or printer (and is
// logged), so no item is dropped.
bool LoadCatalog();

const std::vector<Item>& Items();  // within a subgroup, cheapest first, then by printer order
const std::vector<Group>& Groups();  // display order
const std::vector<Printer>& Printers();  // display order
const Item* FindItem(const std::string& id);
// Index into Printers() of the printer with this ItemCategory_..., or -1.
int FindPrinterByCategory(const std::string& category);

}  // namespace rd
