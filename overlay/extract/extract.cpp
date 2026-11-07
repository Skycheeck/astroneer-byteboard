#include "extract.h"

#include <fstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include "catalog.h"
#include "icons.h"
#include "log.h"
#include "pak.h"
#include "taxonomy.h"
#include "texts.h"
#include "value.h"

// The extraction in steps: find the item types and missions (scan.h), build the catalog (catalog.h),
// decode the icons (icons.h), pick the research entries, look up their texts in the further languages,
// then write the files. An error names the step it happened in.
namespace rd::extract {

namespace {

namespace fs = std::filesystem;

// The columns of catalog.tsv, in the order docs/PROTOCOL.md lists them. Readers find columns by the
// names in the '#' header row. The ...En columns hold the English texts of the Russian ones before them.
constexpr const char* kCatalogColumns[] = {
    "id",     "classPath", "nameRu", "nameEn", "group",      "subgroup",      "printer",  "cost",
    "recipe", "icon",      "description", "pickup", "unlock", "source",       "dlc",      "subgroupEn",
    "descriptionEn", "recipeEn", "sourceEn"};

// "<count>× <name>" joined by ", ", with the ingredient names in one language.
std::string RecipeText(const std::vector<Ingredient>& recipe, std::string Ingredient::*name) {
    std::string text;
    for (size_t k = 0; k < recipe.size(); ++k) {
        if (k) text += ", ";
        text += ToJsString(&recipe[k].count) + "× " + recipe[k].*name;
    }
    return text;
}

// Tabs and line breaks would split a cell or a line, so each run of them becomes one space; then the
// cell is trimmed of white space.
std::string TsvCell(std::string_view text) {
    auto isBreak = [](char c) { return c == '\t' || c == '\r' || c == '\n'; };
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (!isBreak(text[i])) {
            out += text[i];
            continue;
        }
        out += ' ';
        while (i + 1 < text.size() && isBreak(text[i + 1])) ++i;
    }
    return std::string(JsTrim(out));
}

// null becomes an empty cell.
std::string OptionalCell(const std::optional<std::string>& text) {
    return text ? TsvCell(*text) : std::string();
}

void AppendLine(std::string& out, const std::vector<std::string>& cells) {
    for (size_t k = 0; k < cells.size(); ++k) {
        if (k) out += '\t';
        out += cells[k];
    }
    out += '\n';
}

// A '#' header line, then one research entry per line.
std::string CatalogTsv(const ResearchList& list) {
    std::unordered_map<std::string, size_t> iconIndex;
    for (size_t k = 0; k < list.iconNames.size(); ++k) iconIndex.emplace(list.iconNames[k], k);

    std::string out = "#";
    AppendLine(out, std::vector<std::string>(std::begin(kCatalogColumns), std::end(kCatalogColumns)));
    for (const ResearchItem& entry : list.items) {
        const CatalogItem& item = *entry.item;
        const auto icon = item.icon ? iconIndex.find(*item.icon) : iconIndex.end();
        AppendLine(out, {
                            TsvCell(item.id),
                            OptionalCell(item.classPath),
                            OptionalCell(item.ru),
                            OptionalCell(item.en),
                            TsvCell(entry.group ? entry.group : ""),
                            TsvCell(entry.subgroup ? entry.subgroup : ""),
                            TsvCell(item.printer),
                            TsvCell(ToJsString(&item.cost)),
                            TsvCell(RecipeText(item.recipe, &Ingredient::ru)),
                            icon == iconIndex.end() ? "-1" : std::to_string(icon->second),
                            TsvCell(entry.description),
                            OptionalCell(item.pickup),
                            TsvCell(item.unlock),
                            TsvCell(item.source),
                            TsvCell(item.dlc),
                            TsvCell(entry.subgroupEn ? entry.subgroupEn : ""),
                            TsvCell(entry.descriptionEn),
                            TsvCell(RecipeText(item.recipe, &Ingredient::en)),
                            TsvCell(item.sourceEn),
                        });
    }
    return out;
}

// "group <key> <label> <labelEn>" lines, then "printer <key> <category> <label> <short> <labelEn>
// <shortEn>" lines, each in display order.
std::string TaxonomyTsv() {
    std::string out;
    for (const Group& group : Groups()) {
        AppendLine(out, {"group", TsvCell(group.key), TsvCell(group.label), TsvCell(group.labelEn)});
    }
    for (const Printer& printer : Printers()) {
        AppendLine(out, {"printer", TsvCell(printer.key), TsvCell(printer.category), TsvCell(printer.label),
                         TsvCell(printer.shortLabel), TsvCell(printer.labelEn), TsvCell(printer.shortLabelEn)});
    }
    return out;
}

// The languages texts.tsv carries, in its order (English and Russian are in catalog.tsv).
constexpr const char* kTextLanguages[] = {"de", "es-ES", "es", "fr", "it", "nl", "pl", "pt-BR", "tr"};

// A '#' header line, then for each language, each research entry's name, description, recipe and
// mission title in that language. A name, description or title the language has no translation of
// stays empty, and the overlay shows the English text there. Mostly these are texts that read the same
// as in English: a .locres leaves out a translation that equals the source string, and the game then
// shows the source. Recipe ingredients take the source string in that case too, as the game does, so a
// "Zinc" does not turn a whole French recipe English. A language without a Game.locres in the pak has
// no lines.
std::string TextsTsv(const ResearchList& list, Localizer& texts, const Logger& log) {
    std::string out = "#";
    AppendLine(out, {"id", "lang", "name", "description", "recipe", "source"});
    for (const std::string locale : kTextLanguages) {
        if (!texts.AddLanguage(locale)) continue;
        int names = 0;
        int descriptions = 0;
        int recipes = 0;
        int sources = 0;
        int missionItems = 0;
        for (const ResearchItem& entry : list.items) {
            const CatalogItem& item = *entry.item;
            const std::string name = OptionalCell(texts.InLanguage(&item.nameText, locale));
            const std::string description = TsvCell(CleanDescription(texts.InLanguage(&item.descriptionText, locale)));
            std::string recipe;
            for (size_t k = 0; k < item.recipe.size(); ++k) {
                const std::optional<std::string> ingredient = texts.Translate(&item.recipe[k].nameText, locale);
                if (!ingredient) {
                    recipe.clear();
                    break;
                }
                if (k) recipe += ", ";
                recipe += ToJsString(&item.recipe[k].count) + "× " + *ingredient;
            }
            recipe = TsvCell(recipe);
            const bool mission = item.sourceText.type != Value::Type::Null;
            const std::string source = mission ? OptionalCell(texts.InLanguage(&item.sourceText, locale)) : std::string();
            AppendLine(out, {TsvCell(item.id), locale, name, description, recipe, source});
            names += !name.empty();
            descriptions += !description.empty();
            recipes += !recipe.empty() || item.recipe.empty();
            sources += !source.empty();
            missionItems += mission;
        }
        const int count = (int)list.items.size();
        log(locale + ": of " + std::to_string(count) + " items, " + std::to_string(count - names) + " names, " +
            std::to_string(count - descriptions) + " descriptions and " + std::to_string(missionItems - sources) +
            " mission titles have no translation of their own (mostly the same as in English; the overlay shows English " +
            "there), " + std::to_string(count - recipes) + " recipes have an ingredient without a name");
    }
    return out;
}

// One locale folder name per line.
std::string LanguagesTxt(const std::vector<std::string>& languages) {
    std::string out;
    for (const std::string& language : languages) out += language + "\n";
    return out;
}

// Writes to "<name>.tmp", then renames it over the old file, so a reader never sees half a file. The
// overlay opens these files with delete sharing, so the rename works while it reads them.
void WriteReplacing(const fs::path& directory, const char* name, const void* data, size_t size) {
    const fs::path target = directory / name;
    fs::path temporary = target;
    temporary += ".tmp";
    std::error_code ignored;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (out) out.write(static_cast<const char*>(data), (std::streamsize)size);
        if (out) out.close();
        if (!out) {
            fs::remove(temporary, ignored);
            throw ToolError("cannot write " + PathToUtf8(temporary));
        }
    }
    std::error_code error;
    fs::rename(temporary, target, error);
    if (error) {
        fs::remove(temporary, ignored);
        throw ToolError("cannot replace " + PathToUtf8(target) + " (error " + std::to_string(error.value()) + ")");
    }
}

void Fail(Result& result, const char* step, const char* message) noexcept {
    result.ok = false;
    result.items = 0;
    result.icons = 0;
    try {
        result.error = std::string(step) + ": " + message;
    } catch (...) {
        result.error.clear();
    }
}

}  // namespace

Result ExtractGameData(const std::filesystem::path& pakPath, const std::filesystem::path& outDir, LogFn logFn) {
    Result result;
    const char* step = "pak";
    try {
        const Logger log(logFn);
        const Pak pak(pakPath);
        log("pak: " + std::to_string(pak.Files().size()) + " files");

        step = "item types";
        GameAssets assets = ReadGameAssets(pak, log);

        step = "catalog";
        Localizer texts(pak, log);
        const std::vector<CatalogItem> catalog = BuildCatalog(assets, texts, log);
        assets = {};

        step = "icons";
        const IconPixels icons = BuildIcons(pak, catalog, log);

        step = "research list";
        std::unordered_set<std::string> decodedIcons;
        for (const auto& icon : icons) decodedIcons.insert(icon.first);
        const ResearchList list = BuildResearchList(catalog, decodedIcons, log);

        step = "languages";
        const std::vector<std::string> languages = GameLanguages(pak);
        const std::string textsTsv = TextsTsv(list, texts, log);

        step = "files";
        const std::string catalogTsv = CatalogTsv(list);
        const std::string taxonomyTsv = TaxonomyTsv();
        const std::string languagesTxt = LanguagesTxt(languages);
        int width = 0;
        int height = 0;
        const Bytes atlas = BuildAtlas(list.iconNames, icons, width, height);
        std::error_code error;
        fs::create_directories(outDir, error);
        if (error) throw ToolError("cannot create " + PathToUtf8(outDir) + " (error " + std::to_string(error.value()) + ")");
        // catalog.tsv last: the other files refer to its items, or it to their icons and groups.
        WriteReplacing(outDir, "icons.rgba", atlas.data(), atlas.size());
        WriteReplacing(outDir, "taxonomy.tsv", taxonomyTsv.data(), taxonomyTsv.size());
        WriteReplacing(outDir, "languages.txt", languagesTxt.data(), languagesTxt.size());
        WriteReplacing(outDir, "texts.tsv", textsTsv.data(), textsTsv.size());
        WriteReplacing(outDir, "catalog.tsv", catalogTsv.data(), catalogTsv.size());
        log("catalog.tsv: " + std::to_string(list.items.size()) + " items; taxonomy.tsv: " +
            std::to_string(Groups().size()) + " groups, " + std::to_string(Printers().size()) + " printers; icons.rgba: " +
            std::to_string(list.iconNames.size()) + " icons, " + std::to_string(width) + "x" + std::to_string(height) +
            "; languages.txt: " + std::to_string(languages.size()) + " languages");

        result.ok = true;
        result.items = (int)list.items.size();
        result.icons = (int)list.iconNames.size();
    } catch (const std::exception& error) {
        Fail(result, step, error.what());
    } catch (...) {
        // With /EHa this also catches a crash inside the extraction, which then fails instead of
        // taking the game down.
        Fail(result, step, "unexpected internal error");
    }
    return result;
}

}  // namespace rd::extract
