#include "uasset.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace rd::extract {

namespace {

constexpr uint32_t kPackageTag = 0x9E2A83C1;
constexpr int32_t kLegacyFileVersionUe427 = -7;
constexpr uint32_t kPkgFilterEditorOnly = 0x80000000;

constexpr int64_t kGuidSize = 16;
constexpr int64_t kCustomVersionSize = kGuidSize + 4;  // FCustomVersion: key GUID + int32 version
constexpr int64_t kNameHashesSize = 4;                 // two uint16 hashes after each name in the name table
constexpr int64_t kFNameSize = 8;                      // name index + number
constexpr int64_t kImportSize = 28;                    // FObjectImport
constexpr int64_t kImportOuterIndex = 2 * kFNameSize;  // after ClassPackage and ClassName
constexpr int64_t kExportSize = 104;                   // FObjectExport as UE 4.27 writes it in cooked packages
constexpr int kMaxOuterDepth = 32;
// Struct properties nest a few levels in real assets. A deeper chain is a misread; the limit keeps the
// recursion within the stack.
constexpr int kMaxPropertyDepth = 64;

constexpr int8_t kTextHistoryNone = -1;
constexpr int8_t kTextHistoryBase = 0;
constexpr int8_t kTextHistoryStringTableEntry = 11;

// Structs whose tagged-property value is written by the struct's own Serialize, not as tagged properties.
constexpr std::string_view kNativeStructs[] = {
    "Box", "Box2D", "Color", "DateTime", "FrameNumber", "GameplayTagContainer", "Guid", "IntPoint", "IntVector",
    "LinearColor", "Quat", "Rotator", "SoftClassPath", "SoftObjectPath", "Timespan", "Vector", "Vector2D", "Vector4",
};

bool IsNativeStruct(std::string_view name) {
    return std::find(std::begin(kNativeStructs), std::end(kNativeStructs), name) != std::end(kNativeStructs);
}

// FPackageFileSummary up to the import table offset.
struct Summary {
    int32_t totalHeaderSize = 0;
    int32_t nameCount = 0;
    int32_t nameOffset = 0;
    int32_t exportCount = 0;
    int32_t exportOffset = 0;
    int32_t importCount = 0;
    int32_t importOffset = 0;
};

// Only what object paths are built from: the outer and the name. The class of an import is not needed.
struct ImportEntry {
    int32_t outerIndex = 0;
    std::string name;
};

struct ExportEntry {
    int32_t classIndex = 0;
    int32_t superIndex = 0;
    int32_t outerIndex = 0;
    std::string name;
    int64_t serialSize = 0;
    int64_t serialOffset = 0;
};

Summary ReadSummary(Reader& reader) {
    if (reader.U32() != kPackageTag) throw FormatError("not a UE package (wrong tag)");
    const int32_t legacyFileVersion = reader.I32();
    if (legacyFileVersion != kLegacyFileVersionUe427) {
        throw FormatError("package version " + std::to_string(legacyFileVersion) + " is not UE 4.27 (" +
                          std::to_string(kLegacyFileVersionUe427) + ")");
    }
    reader.I32();  // LegacyUE3Version
    reader.I32();  // FileVersionUE4: 0 in unversioned packages, which use the engine's own version
    reader.I32();  // FileVersionLicenseeUE4
    const int32_t customVersionCount = reader.I32();
    reader.Skip((int64_t)customVersionCount * kCustomVersionSize);
    Summary summary;
    summary.totalHeaderSize = reader.I32();
    reader.FString();  // FolderName
    const uint32_t packageFlags = reader.U32();
    summary.nameCount = reader.I32();
    summary.nameOffset = reader.I32();
    if (!(packageFlags & kPkgFilterEditorOnly)) reader.FString();  // LocalizationId, editor packages only
    reader.I32();  // GatherableTextDataCount
    reader.I32();  // GatherableTextDataOffset
    summary.exportCount = reader.I32();
    summary.exportOffset = reader.I32();
    summary.importCount = reader.I32();
    summary.importOffset = reader.I32();
    return summary;
}

// Every loop below reads or seeks for each element, so a wrong count fails at the end of the header
// instead of running on.
std::vector<std::string> ReadNames(Reader& reader, const Summary& summary) {
    reader.Seek(summary.nameOffset);
    std::vector<std::string> names;
    for (int32_t k = 0; k < summary.nameCount; ++k) {
        names.push_back(reader.FString());
        reader.Skip(kNameHashesSize);
    }
    return names;
}

std::vector<ImportEntry> ReadImports(Reader& reader, const Summary& summary, const std::vector<std::string>& names) {
    std::vector<ImportEntry> imports;
    for (int32_t k = 0; k < summary.importCount; ++k) {
        reader.Seek(summary.importOffset + k * kImportSize + kImportOuterIndex);
        ImportEntry entry;
        entry.outerIndex = reader.I32();
        entry.name = ReadName(reader, names);
        imports.push_back(std::move(entry));
    }
    return imports;
}

std::vector<ExportEntry> ReadExports(Reader& reader, const Summary& summary, const std::vector<std::string>& names) {
    std::vector<ExportEntry> exports;
    for (int32_t k = 0; k < summary.exportCount; ++k) {
        reader.Seek(summary.exportOffset + k * kExportSize);
        ExportEntry entry;
        entry.classIndex = reader.I32();
        entry.superIndex = reader.I32();
        reader.I32();  // TemplateIndex
        entry.outerIndex = reader.I32();
        entry.name = ReadName(reader, names);
        reader.U32();  // ObjectFlags
        entry.serialSize = reader.I64();
        entry.serialOffset = reader.I64();
        // The rest of the record (export flags, package GUID and flags, dependency indices) is not needed.
        exports.push_back(std::move(entry));
    }
    return exports;
}

// Object index -> full object path: 0 is null, -1 - k is import k, k + 1 is export k. A path is
// "<package>.<object>", with ":" before a subobject of a top-level object and "." below that
// (UObject::GetPathName).
class PathResolver {
public:
    PathResolver(const std::vector<ImportEntry>& imports, const std::vector<ExportEntry>& exports,
                 const std::string& packagePath)
        : imports_(imports), exports_(exports), packagePath_(packagePath) {}

    std::optional<std::string> PathOf(int32_t index) {
        if (index == 0) return std::nullopt;
        const auto cached = cache_.find(index);
        if (cached != cache_.end()) return cached->second;
        std::vector<const std::string*> chain;
        AppendChain(index, 0, chain);
        std::string path = *chain[0];
        for (size_t k = 1; k < chain.size(); ++k) path += (k == 2 ? ":" : ".") + *chain[k];
        cache_.emplace(index, path);
        return path;
    }

private:
    void AppendChain(int64_t index, int depth, std::vector<const std::string*>& chain) const {
        if (depth > kMaxOuterDepth) throw FormatError("outer chain of object " + std::to_string(index) + " is too deep");
        const int64_t position = index < 0 ? -index - 1 : index - 1;
        const int64_t count = (int64_t)(index < 0 ? imports_.size() : exports_.size());
        if (position < 0 || position >= count) {
            throw FormatError("object index " + std::to_string(index) + " is out of range");
        }
        const int32_t outerIndex = index < 0 ? imports_[position].outerIndex : exports_[position].outerIndex;
        const std::string& name = index < 0 ? imports_[position].name : exports_[position].name;
        if (outerIndex == 0) {
            if (index > 0) chain.push_back(&packagePath_);
        } else {
            AppendChain(outerIndex, depth + 1, chain);
        }
        chain.push_back(&name);
    }

    const std::vector<ImportEntry>& imports_;
    const std::vector<ExportEntry>& exports_;
    const std::string& packagePath_;
    std::unordered_map<int32_t, std::string> cache_;
};

std::string LeafName(const std::string& path) {
    const size_t dot = path.rfind('.');
    const size_t colon = path.rfind(':');
    size_t start = 0;
    if (dot != std::string::npos) start = dot + 1;
    if (colon != std::string::npos) start = std::max(start, colon + 1);
    return path.substr(start);
}

// The tagged properties of one export.
class PropertyReader {
public:
    PropertyReader(const std::vector<std::string>& names, PathResolver& paths, std::vector<std::string>& warnings,
                   const std::string& where)
        : names_(names), paths_(paths), warnings_(warnings), where_(where) {}

    // Tagged properties up to the "None" name or end. The reader is left after "None".
    Value Read(Reader& reader, int64_t end, int depth) {
        if (depth > kMaxPropertyDepth) throw FormatError("properties are nested too deeply");
        Value out = Value::EmptyObject();
        while (reader.Position() < end) {
            const std::optional<Tag> tag = ReadTag(reader);
            if (!tag) break;
            // Seeking back for a negative size could loop forever.
            if (tag->size < 0) throw FormatError("property " + tag->name + " has a negative size");
            const int64_t valueEnd = reader.Position() + tag->size;
            try {
                Value value = ReadValue(reader, *tag, valueEnd, depth);
                out.Set(tag->arrayIndex ? tag->name + "[" + std::to_string(tag->arrayIndex) + "]" : tag->name,
                        std::move(value));
            } catch (const std::exception& error) {
                warnings_.push_back(where_ + ": " + tag->name + " (" + tag->type + "): " + error.what());
            }
            reader.Seek(valueEnd);
        }
        return out;
    }

private:
    // FPropertyTag: name, type, value size, array index, the type's extra fields, optional property GUID.
    struct Tag {
        std::string name;
        std::string type;
        int32_t size = 0;
        int32_t arrayIndex = 0;
        std::string structName;  // StructProperty
        bool boolValue = false;  // BoolProperty
        std::string enumName;    // ByteProperty, EnumProperty
        std::string innerType;   // ArrayProperty, SetProperty, MapProperty
    };

    std::optional<Tag> ReadTag(Reader& reader) {
        Tag tag;
        tag.name = ReadName(reader, names_);
        if (tag.name == "None") return std::nullopt;
        tag.type = ReadName(reader, names_);
        tag.size = reader.I32();
        tag.arrayIndex = reader.I32();
        if (tag.type == "StructProperty") {
            tag.structName = ReadName(reader, names_);
            reader.Skip(kGuidSize);
        } else if (tag.type == "BoolProperty") {
            tag.boolValue = reader.U8() != 0;
        } else if (tag.type == "ByteProperty" || tag.type == "EnumProperty") {
            tag.enumName = ReadName(reader, names_);
        } else if (tag.type == "ArrayProperty" || tag.type == "SetProperty") {
            tag.innerType = ReadName(reader, names_);
        } else if (tag.type == "MapProperty") {
            tag.innerType = ReadName(reader, names_);
            ReadName(reader, names_);  // value type
        }
        if (reader.U8()) reader.Skip(kGuidSize);  // property GUID
        return tag;
    }

    Value ReadValue(Reader& reader, const Tag& tag, int64_t valueEnd, int depth) {
        const std::string& type = tag.type;
        if (type == "IntProperty") return Value::Number(reader.I32());
        if (type == "FloatProperty") return Value::Number(RoundFloat(reader.F32()));
        if (type == "BoolProperty") return Value::Bool(tag.boolValue);
        if (type == "NameProperty") return Value::String(ReadName(reader, names_));
        if (type == "StrProperty") return Value::String(reader.FString());
        if (type == "ObjectProperty" || type == "ClassProperty") return ObjectRef(reader.I32());
        if (type == "SoftObjectProperty") return ReadSoftObjectRef(reader);
        if (type == "ByteProperty" || type == "EnumProperty") {
            if (tag.enumName == "None" && tag.size == 1) return Value::Number(reader.U8());
            return Value::String(ReadName(reader, names_));
        }
        if (type == "TextProperty") return ReadText(reader);
        if (type == "StructProperty") {
            if (IsNativeStruct(tag.structName)) return Value::String("<" + tag.structName + ">");
            return Read(reader, valueEnd, depth + 1);
        }
        if (type == "ArrayProperty") return ReadArray(reader, tag, valueEnd, depth);
        return Value::String("<" + type + " " + std::to_string(tag.size) + "b>");
    }

    Value ObjectRef(int32_t index) {
        const std::optional<std::string> path = paths_.PathOf(index);
        return path ? Value::String("@" + *path) : Value::Null();
    }

    Value ReadSoftObjectRef(Reader& reader) {
        const std::string assetPath = ReadName(reader, names_);
        return Value::String("~" + assetPath + reader.FString());
    }

    // FText: flags, history type, then the history's own fields.
    Value ReadText(Reader& reader) {
        reader.U32();  // flags
        const int8_t historyType = reader.I8();
        switch (historyType) {
        case kTextHistoryNone: {
            const int32_t hasCultureInvariantString = reader.I32();
            return Value::String(hasCultureInvariantString ? reader.FString() : std::string());
        }
        case kTextHistoryBase: {
            std::string ns = reader.FString();
            std::string key = reader.FString();
            std::string src = reader.FString();
            Value text = Value::EmptyObject();
            if (!ns.empty()) text.Set("ns", Value::String(std::move(ns)));
            text.Set("key", Value::String(std::move(key)));
            text.Set("src", Value::String(std::move(src)));
            return text;
        }
        case kTextHistoryStringTableEntry: {
            std::string table = ReadName(reader, names_);
            Value text = Value::EmptyObject();
            text.Set("table", Value::String(std::move(table)));
            text.Set("key", Value::String(reader.FString()));
            return text;
        }
        default:
            throw FormatError("text history type " + std::to_string(historyType) + " is not supported");
        }
    }

    // Each element takes at least elementSize bytes, so a count the value cannot hold is a misread.
    // Refusing it keeps a bad count from allocating or looping without bound.
    static void RequireFit(const Reader& reader, int32_t count, int64_t elementSize, int64_t valueEnd) {
        if (count > 0 && (int64_t)count * elementSize > valueEnd - reader.Position()) {
            throw FormatError(std::to_string(count) + " array elements do not fit in the property");
        }
    }

    static Value Placeholder(const std::string& innerType, int32_t count) {
        Value values = Value::EmptyArray();
        values.array.push_back(Value::String("<" + innerType + " x" + std::to_string(count) + ">"));
        return values;
    }

    Value ReadArray(Reader& reader, const Tag& tag, int64_t valueEnd, int depth) {
        const int32_t count = reader.I32();
        const std::string& inner = tag.innerType;
        Value values = Value::EmptyArray();
        if (inner == "ObjectProperty") {
            RequireFit(reader, count, 4, valueEnd);
            for (int32_t k = 0; k < count; ++k) values.array.push_back(ObjectRef(reader.I32()));
        } else if (inner == "IntProperty") {
            RequireFit(reader, count, 4, valueEnd);
            for (int32_t k = 0; k < count; ++k) values.array.push_back(Value::Number(reader.I32()));
        } else if (inner == "NameProperty" || inner == "EnumProperty") {
            RequireFit(reader, count, kFNameSize, valueEnd);
            for (int32_t k = 0; k < count; ++k) values.array.push_back(Value::String(ReadName(reader, names_)));
        } else if (inner == "ByteProperty") {
            // An enum byte array stores FNames (8 bytes each); a plain byte array is not decoded.
            if ((int64_t)tag.size != 4 + (int64_t)count * kFNameSize) return Placeholder(inner, count);
            for (int32_t k = 0; k < count; ++k) values.array.push_back(Value::String(ReadName(reader, names_)));
        } else if (inner == "SoftObjectProperty") {
            RequireFit(reader, count, kFNameSize + 4, valueEnd);
            for (int32_t k = 0; k < count; ++k) values.array.push_back(ReadSoftObjectRef(reader));
        } else if (inner == "StructProperty") {
            return ReadStructArray(reader, count, valueEnd, depth);
        } else {
            return Placeholder(inner, count);
        }
        return values;
    }

    // A struct array repeats a full property tag for its elements (name, type, size, array index, struct
    // name, struct GUID, property GUID flag), then holds the elements one after another.
    Value ReadStructArray(Reader& reader, int32_t count, int64_t valueEnd, int depth) {
        ReadName(reader, names_);  // element tag name
        ReadName(reader, names_);  // element tag type, StructProperty
        reader.I32();              // size of all elements
        reader.I32();              // array index
        const std::string structName = ReadName(reader, names_);
        reader.Skip(kGuidSize);
        if (reader.U8()) reader.Skip(kGuidSize);
        const bool native = IsNativeStruct(structName);
        // A tagged element ends with at least its "None" name.
        RequireFit(reader, count, native ? 1 : kFNameSize, valueEnd);
        Value values = Value::EmptyArray();
        for (int32_t k = 0; k < count; ++k) {
            values.array.push_back(native ? Value::String("<" + structName + ">") : Read(reader, valueEnd, depth + 1));
        }
        return values;
    }

    const std::vector<std::string>& names_;
    PathResolver& paths_;
    std::vector<std::string>& warnings_;
    const std::string& where_;
};

}  // namespace

const Export* Asset::FindByClass(std::string_view className) const {
    for (const Export& e : exports) {
        if (e.className && *e.className == className) return &e;
    }
    return nullptr;
}

Asset ParseAsset(ByteView uasset, ByteView uexp, const std::string& packagePath) {
    Reader header(uasset);
    const Summary summary = ReadSummary(header);
    Asset asset;
    asset.names = ReadNames(header, summary);
    const std::vector<ImportEntry> imports = ReadImports(header, summary, asset.names);
    const std::vector<ExportEntry> entries = ReadExports(header, summary, asset.names);
    PathResolver paths(imports, entries, packagePath);

    for (size_t k = 0; k < entries.size(); ++k) {
        const ExportEntry& entry = entries[k];
        const int64_t dataStart = entry.serialOffset - summary.totalHeaderSize;
        const int64_t dataEnd = dataStart + entry.serialSize;
        Export result;
        result.name = entry.name;
        result.classPath = paths.PathOf(entry.classIndex);
        result.path = *paths.PathOf((int32_t)(k + 1));
        result.superPath = paths.PathOf(entry.superIndex);
        if (result.classPath) result.className = LeafName(*result.classPath);
        PropertyReader properties(asset.names, paths, asset.warnings, entry.name);
        try {
            if (dataStart < 0 || dataEnd > (int64_t)uexp.size()) throw FormatError("export data is outside the .uexp");
            Reader reader(uexp, dataStart);
            result.props = properties.Read(reader, dataEnd, 0);
            result.propertiesEnd = reader.Position();
        } catch (const std::exception& error) {
            asset.warnings.push_back(entry.name + ": " + error.what());
        }
        asset.exports.push_back(std::move(result));
    }
    return asset;
}

// An FName is an index into the name table plus a number; number N > 0 stands for the suffix "_<N-1>".
std::string ReadName(Reader& reader, const std::vector<std::string>& names) {
    const int32_t index = reader.I32();
    const int32_t number = reader.I32();
    if (index < 0 || (size_t)index >= names.size()) {
        throw FormatError("name index " + std::to_string(index) + " is out of range");
    }
    return number ? names[index] + "_" + std::to_string((int64_t)number - 1) : names[index];
}

void SkipObjectGuid(Reader& reader) {
    if (reader.U32()) reader.Skip(kGuidSize);
}

std::optional<std::string> RefPath(const Value* value) {
    if (!value || value->type != Value::Type::String || value->string.size() < 2 || value->string[0] != '@') {
        return std::nullopt;
    }
    return value->string.substr(1);
}

std::string PackageToPakPath(std::string_view packagePath) {
    constexpr std::string_view kGameRoot = "/Game/";
    if (!packagePath.starts_with(kGameRoot)) throw std::runtime_error("not a /Game/ package: " + std::string(packagePath));
    return "Astro/Content/" + std::string(packagePath.substr(kGameRoot.size()));
}

std::string PakPathToPackage(std::string_view pakPath) {
    constexpr std::string_view kContentDir = "Astro/Content/";
    if (!pakPath.starts_with(kContentDir)) throw std::runtime_error("not under Astro/Content/: " + std::string(pakPath));
    std::string path(pakPath.substr(kContentDir.size()));
    // Without the extension: /\.[^./]+$/
    const size_t dot = path.rfind('.');
    if (dot != std::string::npos && dot + 1 < path.size() && path.find('/', dot) == std::string::npos) path.erase(dot);
    return "/Game/" + path;
}

std::string PackageOf(std::string_view objectPath) {
    return std::string(objectPath.substr(0, objectPath.find('.')));
}

std::string ObjectName(std::string_view objectPath) {
    const size_t dot = objectPath.rfind('.');
    std::string_view name = dot == std::string_view::npos ? objectPath : objectPath.substr(dot + 1);
    if (name.ends_with("_C")) name.remove_suffix(2);
    return std::string(name);
}

}  // namespace rd::extract
