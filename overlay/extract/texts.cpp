#include "texts.h"

#include <cstring>

#include "uasset.h"

namespace rd::extract {

namespace {

constexpr const char* kLocresEn = "Astro/Content/Localization/Game/en/Game.locres";
constexpr const char* kLocresRu = "Astro/Content/Localization/Game/ru/Game.locres";

// FGuid(0x7574140E, 0xFC034A67, 0x9D90154A, 0x1B7F37C3), stored as four little-endian uint32.
constexpr uint8_t kLocresMagic[16] = {0x0E, 0x14, 0x74, 0x75, 0x67, 0x4A, 0x03, 0xFC,
                                      0x4A, 0x15, 0x90, 0x9D, 0xC3, 0x37, 0x7F, 0x1B};
constexpr uint8_t kVersionOptimizedCrc32 = 2;
constexpr uint8_t kVersionOptimizedCityHash64 = 3;
constexpr int64_t kNoStringArray = -1;

}  // namespace

Localizer::Localizer(const Pak& pak, const Logger& log) : pak_(pak), log_(log) {
    en_ = ParseLocres(pak.Read(kLocresEn));
    ru_ = ParseLocres(pak.Read(kLocresRu));
}

// A compiled localization file as UE 4.27 writes it (FTextLocalizationResource::LoadFromArchive),
// versions 2 (Optimized_CRC32) and 3 (Optimized_CityHash64_UTF16), the formats UE 4.20+ writes.
Localizer::Locres Localizer::ParseLocres(ByteView data) {
    if (data.size() < sizeof kLocresMagic || std::memcmp(data.data(), kLocresMagic, sizeof kLocresMagic) != 0) {
        throw FormatError("not a .locres file, or a legacy one without the magic number");
    }
    Reader reader(data, sizeof kLocresMagic);
    const uint8_t version = reader.U8();
    if (version != kVersionOptimizedCrc32 && version != kVersionOptimizedCityHash64) {
        throw FormatError("unsupported .locres version " + std::to_string(version));
    }
    const int64_t stringArrayOffset = reader.I64();
    reader.U32();  // entry count

    // The localized strings, each followed by its reference count.
    std::vector<std::string> strings;
    if (stringArrayOffset != kNoStringArray) {
        Reader stringReader(data, stringArrayOffset);
        const int32_t count = stringReader.I32();
        for (int32_t k = 0; k < count; ++k) {
            strings.push_back(stringReader.FString());
            stringReader.I32();  // reference count
        }
    }

    Locres locres;
    const uint32_t namespaceCount = reader.U32();
    for (uint32_t n = 0; n < namespaceCount; ++n) {
        reader.U32();  // namespace hash
        auto& entries = locres[reader.FString()];
        const uint32_t keyCount = reader.U32();
        for (uint32_t k = 0; k < keyCount; ++k) {
            reader.U32();  // key hash
            std::string key = reader.FString();
            reader.U32();  // source string hash
            const int32_t index = reader.I32();
            entries[std::move(key)] = index >= 0 && (size_t)index < strings.size() ? strings[index] : std::string();
        }
    }
    return locres;
}

// A cooked string table asset (UStringTable, UE 4.27 FStringTable::Serialize). tablePath is the
// table's object path from a TextProperty, e.g. "/Game/Globals/StringTables/Items.Items".
Localizer::StringTable Localizer::ReadStringTable(const std::string& tablePath) const {
    const std::string packagePath = PackageOf(tablePath);
    const std::string file = PackageToPakPath(packagePath);
    const Bytes uexp = pak_.Read(file + ".uexp");
    const Asset asset = ParseAsset(pak_.Read(file + ".uasset"), uexp, packagePath);
    const Export* table = asset.FindByClass("StringTable");
    if (!table || !table->propertiesEnd) throw FormatError("no readable StringTable export in " + file);

    // After the object's tagged properties: the object GUID, then TableNamespace, the entry count and
    // the key/source string pairs (the entries' metadata follows and is not needed).
    Reader reader(uexp, *table->propertiesEnd);
    SkipObjectGuid(reader);
    StringTable result;
    result.ns = reader.FString();
    const int32_t count = reader.I32();
    for (int32_t k = 0; k < count; ++k) {
        std::string key = reader.FString();
        result.entries[std::move(key)] = reader.FString();
    }
    return result;
}

const Localizer::StringTable* Localizer::StringTableAt(const std::string& tablePath) {
    auto found = tables_.find(tablePath);
    if (found == tables_.end()) {
        std::optional<StringTable> table;
        try {
            table = ReadStringTable(tablePath);
        } catch (const std::exception& error) {
            log_("string table " + tablePath + " could not be read: " + error.what());
        }
        found = tables_.emplace(tablePath, std::move(table)).first;
    }
    return found->second ? &*found->second : nullptr;
}

const std::string* Localizer::Find(const Locres& locres, const std::string& ns, const std::string& key) {
    const auto entries = locres.find(ns);
    if (entries == locres.end()) return nullptr;
    const auto entry = entries->second.find(key);
    return entry == entries->second.end() ? nullptr : &entry->second;
}

std::optional<std::string> Localizer::Localize(const Value* text, Language language) {
    if (!text || text->type == Value::Type::Null) return std::nullopt;
    if (text->type == Value::Type::String) return text->string;  // a culture-invariant text
    const Locres& strings = language == Language::En ? en_ : ru_;
    std::unordered_set<std::string>& untranslated = untranslated_[language == Language::En ? 0 : 1];

    if (const Value* tableValue = text->Get("table")) {
        const std::string tablePath = ToJsString(tableValue);
        const std::string key = ToJsString(text->Get("key"));
        if (const StringTable* table = StringTableAt(tablePath)) {
            const std::string* localized = Find(strings, table->ns, key);
            if (localized && !localized->empty()) return *localized;
            const auto source = table->entries.find(key);
            if (source != table->entries.end() && !source->second.empty()) {
                untranslated.insert(tablePath + " " + key);
                return source->second;
            }
        }
        if (unresolved_.insert(tablePath + " " + key).second) unresolvedInOrder_.push_back(tablePath + " " + key);
        return key;
    }
    if (const Value* keyValue = text->Get("key")) {
        const Value* nsValue = text->Get("ns");
        const std::string ns = nsValue && nsValue->type != Value::Type::Null ? ToJsString(nsValue) : std::string();
        const std::string key = ToJsString(keyValue);
        const std::string* localized = Find(strings, ns, key);
        if (localized && !localized->empty()) return *localized;
        const Value* source = text->Get("src");
        if (Truthy(source)) untranslated.insert(ns + " " + key);
        if (!source || source->type == Value::Type::Null) return std::nullopt;
        return ToJsString(source);
    }
    return std::nullopt;
}

void Localizer::ReportMisses() const {
    const char* languages[2] = {"en", "ru"};
    for (int k = 0; k < 2; ++k) {
        if (!untranslated_[k].empty()) {
            log_(std::to_string(untranslated_[k].size()) + " texts have no " + languages[k] +
                 " translation and use the source string");
        }
    }
    if (!unresolvedInOrder_.empty()) {
        log_(std::to_string(unresolvedInOrder_.size()) + " string table entries were not found; the key is shown instead:");
        for (const std::string& entry : unresolvedInOrder_) log_("  " + entry);
    }
}

}  // namespace rd::extract
