#include "taxonomy.h"

#include <iterator>

namespace rd::extract {

namespace {

constexpr Group kGroups[] = {
    {"power", "Энергия", "Power"},
    {"oxygen", "Кислород", "Oxygen"},
    {"prod", "Производство", "Production"},
    {"storage", "Хранение", "Storage"},
    {"base", "Платформы и база", "Platforms & Base"},
    {"auto", "Автоматизация", "Automation"},
    {"transport", "Транспорт", "Transport"},
    {"terrain", "Терраформинг", "Terraforming"},
    {"explore", "Разведка и снаряжение", "Exploration & Gear"},
    {"fun", "Отдых и декор", "Leisure & Decor"},
};

constexpr Printer kPrinters[] = {
    {"Backpack", "ItemCategory_Backpack", "Рюкзак", "рюкзак", "Backpack", "backpack"},
    {"SmFab", "ItemCategory_SmFab", "Малый принтер", "малый", "Small Printer", "small"},
    {"MedFab", "ItemCategory_MedFab", "Средний принтер", "средний", "Medium Printer", "medium"},
    {"LgFab", "ItemCategory_LgFab", "Большой принтер", "большой", "Large Printer", "large"},
};

// A pattern in regular expression notation, restricted to what the rules need: literal alternatives
// separated by '|', each optionally with '^' and/or \b before it and \b and/or '$' after it, or \b(...)\b
// around all of them. Optional characters are written out as two alternatives ("Lava lamp|Lavalamp").
// ignoreCase folds ASCII letters only and \b uses the ASCII word characters, so UTF-8 text is matched
// byte by byte.
struct Pattern {
    std::string_view source;
    bool ignoreCase;
};

// A rule puts the matching items into a group and a subgroup, labelled in Russian and in English.
struct Rule {
    Pattern pattern;
    const char* group;
    const char* subgroup;
    const char* subgroupEn;
};

constexpr Rule kRules[] = {
    // Items of the expansions and of the vehicle updates, ahead of the generic rules below.
    {{R"(\bRail)", true}, "transport", "Железная дорога", "Railway"},
    {{"Walkway", true}, "base", "Дорожки", "Walkways"},
    {{"VTOL", true}, "transport", "Летательные аппараты", "Aircraft"},
    {{"Hoverboard", true}, "explore", "Передвижение", "Movement"},
    {{"Snow Globe|Bottle|Aquarium|Model Planet|Lava lamp|Lavalamp|Bonsai|Icon Sign|Mushroom", true}, "fun",
     "Отдых и декор", "Leisure & Decor"},
    {{"Megastructure", true}, "prod", "Мегаструктуры", "Megastructures"},
    {{"Fault Finder", true}, "explore", "Поиск", "Scanners & Beacons"},
    {{"Ruby|D-Cipher|DCipher", true}, "explore", "Ключевые предметы", "Key Items"},

    {{R"(\b(Ring|Hoop|Arch|Canopy)\b)", true}, "auto", "Сенсорные кольца", "Sensor Rings"},
    {{"Sensor|Repeat|Proximity|Delay|Count|Button", true}, "auto", "Датчики и логика", "Sensors & Logic"},
    {{"Horn|Hologram|Figurine", true}, "fun", "Отдых и декор", "Leisure & Decor"},
    {{"Seat|Crane|Winch", true}, "transport", "Навесное", "Vehicle Attachments"},
    {{"Tapper|Arm|Extractor|Hopper", true}, "auto", "Механизмы", "Mechanisms"},
    {{"Solar", true}, "power", "Солнечные панели", "Solar Panels"},
    {{"Wind", true}, "power", "Ветротурбины", "Wind Turbines"},
    {{"Generator$|RTG", true}, "power", "Генераторы", "Generators"},
    {{"Battery|Power Cells", true}, "power", "Батареи", "Batteries"},
    {{"Splitter|Extenders|Switch", true}, "power", "Проводка", "Wiring"},
    {{"Oxygenator|Oxygen Filters|Oxygen Tank", true}, "oxygen", "Кислород", "Oxygen"},
    {{"Tether", true}, "oxygen", "Тросы", "Tethers"},  // the tethers and the tether dropper
    {{"Printer$", true}, "prod", "Принтеры", "Printers"},
    {{"Print Filter", true}, "prod", "Фильтры печати", "Print Filters"},
    {{"Smelt|Chemistry|Centrifuge|Condenser", true}, "prod", "Переработка", "Processing"},
    {{"Shredder", true}, "prod", "Шреддеры", "Shredders"},
    {{"Trade|EXO Request|Packager", true}, "prod", "Торговля и упаковка", "Trade & Packaging"},
    {{"Research", true}, "prod", "Исследования", "Research"},
    {{"Canister", true}, "storage", "Контейнеры", "Canisters"},
    {{"Storage|Silo", true}, "storage", "Склады", "Storage"},
    {{"Shelter", true}, "base", "Убежища", "Shelters"},
    {{"Landing", true}, "base", "Посадка", "Landing Pads"},
    {{"Platform", true}, "base", "Платформы", "Platforms"},
    {{"Shuttle", true}, "transport", "Корабли", "Shuttles"},
    {{"Thruster", true}, "transport", "Двигатели", "Thrusters"},
    {{"Rover|Buggy|Tractor|Trailer", true}, "transport", "Машины", "Vehicles"},
    {{"Drill Strength|Paver", true}, "terrain", "Буры для транспорта", "Vehicle Drills"},
    {{"Drill|Augment|Boost|Alignment|Brush|Inhibitor|Terrain Analyzer|Leveling", true}, "terrain",
     "Модули терраформера", "Terrain Tool Mods"},
    {{"Jump|Jetpack|Drone|Aero", true}, "explore", "Передвижение", "Movement"},
    {{"Beacon|Scanner|Seeker|Probe", true}, "explore", "Поиск", "Scanners & Beacons"},
    {{"Light|Glowstick|Flare", true}, "explore", "Свет", "Lights"},
    {{"Dynamite", true}, "explore", "Взрывчатка", "Explosives"},
    {{"Camera|Firework|Sphere|Display", true}, "fun", "Отдых и декор", "Leisure & Decor"},
};

// The starter habitats (but not the cosmetic shelter that only shares the name), wrecked and event
// copies, creative plinths, and two hidden copies of other entries with the same name, cost and
// printer: LiquidCanister_Fixed_T3_IT (of LiquidCanister_T3_IT) and GW_DCipher_NoPower_IT (of
// GW_DCipher_IT).
constexpr Pattern kExclude = {"Starter$|Starter_IT$|SemiWrecked|_GW_|CanisterPlinth|_NoPower|^LiquidCanister_Fixed_T3_IT$", false};

// Aero, the camera drone. The Lua mod refuses to unlock the same ids (its CREATIVE_ONLY list).
constexpr std::string_view kCreativeOnly[] = {"CreativeDroneIT"};

constexpr std::string_view kWordBoundary = R"(\b)";

bool IsWordByte(std::string_view text, size_t i) {
    if (i >= text.size()) return false;
    const char c = text[i];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool IsWordBoundary(std::string_view text, size_t i) {
    return (i > 0 && IsWordByte(text, i - 1)) != IsWordByte(text, i);
}

char FoldAscii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

bool LiteralAt(std::string_view text, size_t at, std::string_view literal, bool ignoreCase) {
    for (size_t k = 0; k < literal.size(); ++k) {
        const char a = text[at + k];
        const char b = literal[k];
        if (ignoreCase ? FoldAscii(a) != FoldAscii(b) : a != b) return false;
    }
    return true;
}

// True when any alternative matches anywhere in text.
bool Test(const Pattern& pattern, std::string_view text) {
    std::string_view source = pattern.source;
    const bool allBounded = source.starts_with(R"(\b()") && source.ends_with(R"()\b)");
    if (allBounded) source = source.substr(3, source.size() - 6);
    while (true) {
        const size_t bar = source.find('|');
        std::string_view alternative = source.substr(0, bar);
        const bool atStart = alternative.starts_with('^');
        if (atStart) alternative.remove_prefix(1);
        const bool boundedBefore = allBounded || alternative.starts_with(kWordBoundary);
        if (alternative.starts_with(kWordBoundary)) alternative.remove_prefix(kWordBoundary.size());
        const bool atEnd = alternative.ends_with('$');
        if (atEnd) alternative.remove_suffix(1);
        const bool boundedAfter = allBounded || alternative.ends_with(kWordBoundary);
        if (alternative.ends_with(kWordBoundary)) alternative.remove_suffix(kWordBoundary.size());
        for (size_t at = 0; at + alternative.size() <= text.size(); ++at) {
            if (atStart && at > 0) break;
            const size_t end = at + alternative.size();
            if (atEnd && end != text.size()) continue;
            if (!LiteralAt(text, at, alternative, pattern.ignoreCase)) continue;
            if (boundedBefore && !IsWordBoundary(text, at)) continue;
            if (boundedAfter && !IsWordBoundary(text, end)) continue;
            return true;
        }
        if (bar == std::string_view::npos) return false;
        source.remove_prefix(bar + 1);
    }
}

}  // namespace

std::span<const Group> Groups() { return kGroups; }
std::span<const Printer> Printers() { return kPrinters; }

const Printer* FindPrinter(std::string_view category) {
    for (const Printer& printer : kPrinters) {
        if (category == printer.category) return &printer;
    }
    return nullptr;
}

Classification Classify(std::string_view nameEn, std::string_view id) {
    for (std::string_view text : {nameEn, id}) {
        for (const Rule& rule : kRules) {
            if (Test(rule.pattern, text)) return {rule.group, rule.subgroup, rule.subgroupEn};
        }
    }
    return {};
}

bool IsExcluded(std::string_view id) {
    return Test(kExclude, id);
}

bool IsCreativeOnly(std::string_view id) {
    for (std::string_view creativeOnly : kCreativeOnly) {
        if (id == creativeOnly) return true;
    }
    return false;
}

}  // namespace rd::extract
