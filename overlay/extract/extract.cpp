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
#include "value.h"

// The port of `node tools/extract.mjs` (tools/steps/*.mjs) for the files the overlay reads: it runs the
// same steps in the same order, with the same names in its error messages, and leaves out what only
// feeds data/*.json.
namespace rd::extract {

namespace {

namespace fs = std::filesystem;

// The columns of catalog.tsv, in the order docs/PROTOCOL.md lists them.
constexpr const char* kCatalogColumns[] = {"id",      "classPath", "nameRu", "nameEn", "group",       "subgroup",
                                           "printer", "cost",      "recipe", "icon",   "description", "pickup"};

// Tabs and line breaks would split a cell or a line, so each run of them becomes one space; then the
// cell is trimmed (export-overlay.mjs tsvCell).
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
        std::string recipe;  // "<count>× <name>" joined by ", "
        for (size_t k = 0; k < item.recipe.size(); ++k) {
            if (k) recipe += ", ";
            recipe += ToJsString(&item.recipe[k].count) + "× " + item.recipe[k].ru;
        }
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
                            TsvCell(recipe),
                            icon == iconIndex.end() ? "-1" : std::to_string(icon->second),
                            TsvCell(entry.description),
                            OptionalCell(item.pickup),
                        });
    }
    return out;
}

// "group <key> <label>" lines, then "printer <key> <category> <label> <short>" lines, each in display order.
std::string TaxonomyTsv() {
    std::string out;
    for (const Group& group : Groups()) AppendLine(out, {"group", TsvCell(group.key), TsvCell(group.label)});
    for (const Printer& printer : Printers()) {
        AppendLine(out, {"printer", TsvCell(printer.key), TsvCell(printer.category), TsvCell(printer.label),
                         TsvCell(printer.shortLabel)});
    }
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
    const char* step = "extract";
    try {
        const Logger log(logFn);
        const Pak pak(pakPath);
        log("pak: " + std::to_string(pak.Files().size()) + " files");

        step = "dump-itemtypes";
        std::vector<ItemType> itemTypes = ReadItemTypes(pak, log);

        step = "build-catalog";
        const std::vector<CatalogItem> catalog = BuildCatalog(pak, itemTypes, log);
        itemTypes = {};

        step = "build-icons";
        const IconPixels icons = BuildIcons(pak, catalog, log);

        step = "build-items";
        std::unordered_set<std::string> decodedIcons;
        for (const auto& icon : icons) decodedIcons.insert(icon.first);
        const ResearchList list = BuildResearchList(catalog, decodedIcons, log);

        step = "export-overlay";
        const std::string catalogTsv = CatalogTsv(list);
        const std::string taxonomyTsv = TaxonomyTsv();
        int width = 0;
        int height = 0;
        const Bytes atlas = BuildAtlas(list.iconNames, icons, width, height);
        std::error_code error;
        fs::create_directories(outDir, error);
        if (error) throw ToolError("cannot create " + PathToUtf8(outDir) + " (error " + std::to_string(error.value()) + ")");
        // catalog.tsv last: it refers to the icons and the groups.
        WriteReplacing(outDir, "icons.rgba", atlas.data(), atlas.size());
        WriteReplacing(outDir, "taxonomy.tsv", taxonomyTsv.data(), taxonomyTsv.size());
        WriteReplacing(outDir, "catalog.tsv", catalogTsv.data(), catalogTsv.size());
        log("catalog.tsv: " + std::to_string(list.items.size()) + " items; taxonomy.tsv: " +
            std::to_string(Groups().size()) + " groups, " + std::to_string(Printers().size()) + " printers; icons.rgba: " +
            std::to_string(list.iconNames.size()) + " icons, " + std::to_string(width) + "x" + std::to_string(height));

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
