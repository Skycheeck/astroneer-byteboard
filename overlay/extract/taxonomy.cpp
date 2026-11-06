#include "taxonomy.h"

#include <iterator>

namespace rd::extract {

namespace {

constexpr Group kGroups[] = {
    {"power", "Энергия"},
    {"oxygen", "Кислород"},
    {"prod", "Производство"},
    {"storage", "Хранение"},
    {"base", "Платформы и база"},
    {"auto", "Автоматизация"},
    {"transport", "Транспорт"},
    {"terrain", "Терраформинг"},
    {"explore", "Разведка и снаряжение"},
    {"fun", "Отдых и декор"},
};

constexpr Printer kPrinters[] = {
    {"Backpack", "ItemCategory_Backpack", "Рюкзак", "рюкзак"},
    {"SmFab", "ItemCategory_SmFab", "Малый принтер", "малый"},
    {"MedFab", "ItemCategory_MedFab", "Средний принтер", "средний"},
    {"LgFab", "ItemCategory_LgFab", "Большой принтер", "большой"},
};

// The JavaScript regular expressions of taxonomy.mjs, written the same way. They only use this subset:
// literal alternatives separated by '|', '^' before or '$' after an alternative, and \b(...)\b around
// all of them. Without the u flag, JavaScript's /i folds only ASCII letters here (no other character
// case-folds to an ASCII one) and \b uses the ASCII word characters, so matching UTF-8 bytes is exact.
struct Pattern {
    std::string_view source;
    bool ignoreCase;
};

struct Rule {
    Pattern pattern;
    const char* group;
    const char* subgroup;
};

constexpr Rule kRules[] = {
    {{R"(\b(Ring|Hoop|Arch|Canopy)\b)", true}, "auto", "Сенсорные кольца"},
    {{"Sensor|Repeat|Proximity|Delay|Count|Button", true}, "auto", "Датчики и логика"},
    {{"Horn|Hologram|Figurine", true}, "fun", "Отдых и декор"},
    {{"Seat|Crane|Winch", true}, "transport", "Навесное"},
    {{"Tapper|Arm|Extractor|Hopper", true}, "auto", "Механизмы"},
    {{"Solar", true}, "power", "Солнечные панели"},
    {{"Wind", true}, "power", "Ветротурбины"},
    {{"Generator$|RTG", true}, "power", "Генераторы"},
    {{"Battery|Power Cells", true}, "power", "Батареи"},
    {{"Splitter|Extenders|Switch", true}, "power", "Проводка"},
    {{"Oxygenator|Oxygen Filters|Oxygen Tank", true}, "oxygen", "Кислород"},
    {{"Tethers", true}, "oxygen", "Тросы"},
    {{"Printer$", true}, "prod", "Принтеры"},
    {{"Print Filter", true}, "prod", "Фильтры печати"},
    {{"Smelt|Chemistry|Centrifuge|Condenser", true}, "prod", "Переработка"},
    {{"Shredder", true}, "prod", "Шреддеры"},
    {{"Trade|EXO Request|Packager", true}, "prod", "Торговля и упаковка"},
    {{"Research", true}, "prod", "Исследования"},
    {{"Canister", true}, "storage", "Контейнеры"},
    {{"Storage|Silo", true}, "storage", "Склады"},
    {{"Shelter", true}, "base", "Убежища"},
    {{"Landing", true}, "base", "Посадка"},
    {{"Platform", true}, "base", "Платформы"},
    {{"Shuttle", true}, "transport", "Корабли"},
    {{"Thruster", true}, "transport", "Двигатели"},
    {{"Rover|Buggy|Tractor|Trailer", true}, "transport", "Машины"},
    {{"Drill Strength|Paver", true}, "terrain", "Буры для транспорта"},
    {{"Drill|Augment|Boost|Alignment|Brush|Inhibitor|Terrain Analyzer|Leveling", true}, "terrain", "Модули терраформера"},
    {{"Jump|Jetpack|Drone|Aero", true}, "explore", "Передвижение"},
    {{"Beacon|Scanner|Seeker|Probe", true}, "explore", "Поиск"},
    {{"Light|Glowstick|Flare", true}, "explore", "Свет"},
    {{"Dynamite", true}, "explore", "Взрывчатка"},
    {{"Camera|Firework|Sphere|Display", true}, "fun", "Отдых и декор"},
};

// The starter habitat, wrecked and event copies, creative plinths, and LiquidCanister_Fixed_T3_IT, a
// hidden copy of LiquidCanister_T3_IT with the same name, cost and printer.
constexpr Pattern kExclude = {"Starter|SemiWrecked|_GW_|CanisterPlinth|^LiquidCanister_Fixed_T3_IT$", false};

// Aero, the camera drone. The Lua mod refuses to unlock the same ids (its CREATIVE_ONLY list).
constexpr std::string_view kCreativeOnly[] = {"CreativeDroneIT"};

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

// RegExp.prototype.test: true when any alternative matches anywhere.
bool Test(const Pattern& pattern, std::string_view text) {
    std::string_view source = pattern.source;
    const bool wordBounded = source.starts_with(R"(\b()") && source.ends_with(R"()\b)");
    if (wordBounded) source = source.substr(3, source.size() - 6);
    while (true) {
        const size_t bar = source.find('|');
        std::string_view alternative = source.substr(0, bar);
        const bool atStart = alternative.starts_with('^');
        if (atStart) alternative.remove_prefix(1);
        const bool atEnd = alternative.ends_with('$');
        if (atEnd) alternative.remove_suffix(1);
        for (size_t at = 0; at + alternative.size() <= text.size(); ++at) {
            if (atStart && at > 0) break;
            const size_t end = at + alternative.size();
            if (atEnd && end != text.size()) continue;
            if (!LiteralAt(text, at, alternative, pattern.ignoreCase)) continue;
            if (wordBounded && !(IsWordBoundary(text, at) && IsWordBoundary(text, end))) continue;
            return true;
        }
        if (bar == std::string_view::npos) return false;
        source.remove_prefix(bar + 1);
    }
}

}  // namespace

std::span<const Group> Groups() { return kGroups; }
std::span<const Printer> Printers() { return kPrinters; }

Classification Classify(std::string_view nameEn, std::string_view id) {
    for (std::string_view text : {nameEn, id}) {
        for (const Rule& rule : kRules) {
            if (Test(rule.pattern, text)) return {rule.group, rule.subgroup};
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
