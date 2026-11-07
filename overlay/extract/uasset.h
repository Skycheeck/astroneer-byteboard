#pragma once
// Parses a cooked, unversioned UE 4.27 package: the .uasset header (package
// summary, name, import and export tables) and the tagged properties of every export, which a cooked
// split package keeps in the .uexp. Property values:
//   Int, Float (rounded to 3 decimals), Bool, Name and Str properties: plain values
//   Object and Class properties: "@" + full object path, e.g. "@/Game/Items/ItemTypes/Minables/Zinc.Zinc_C";
//     null for a null reference
//   SoftObjectProperty: "~" + asset path + sub-path
//   Byte and Enum properties: the enum value name, e.g. "EItemVariantType::Engineer", or the number of a
//     plain byte
//   TextProperty: {key, src} for a base text ({ns, key, src} when its namespace is not empty),
//     {table, key} for a string table entry, or the string of a culture-invariant text
//   StructProperty: an object of the struct's tagged properties, or "<Name>" for a natively serialized struct
//   ArrayProperty: an array of such values; ["<Type xN>"] for element types the reader does not decode
//   any other property type: "<Type Nb>"
// A property that cannot be decoded is left out and described in warnings.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reader.h"
#include "value.h"

namespace rd::extract {

struct Export {
    std::string name;
    std::string path;
    std::optional<std::string> classPath;
    std::optional<std::string> className;
    std::optional<std::string> superPath;
    Value props = Value::EmptyObject();
    // Where the export's tagged properties end in the .uexp; native serialization (a string table, the
    // texture data) follows there. Empty when the properties could not be read.
    std::optional<int64_t> propertiesEnd;
};

struct Asset {
    std::vector<std::string> names;
    std::vector<Export> exports;
    std::vector<std::string> warnings;

    // The first export of this class, or nullptr.
    const Export* FindByClass(std::string_view className) const;
};

// packagePath is like "/Game/Items/ItemTypes/Components/Zinc". Throws FormatError when the header
// cannot be read; an export whose properties cannot be read gets a warning instead.
Asset ParseAsset(ByteView uasset, ByteView uexp, const std::string& packagePath);

// For readers of the native data after the properties.
std::string ReadName(Reader& reader, const std::vector<std::string>& names);
// UObject::Serialize ends with a bool (4 bytes) and, when it is set, the object's GUID.
void SkipObjectGuid(Reader& reader);

// The object path of an Object/Class property value ("@path"), or empty for a null reference, any
// other value, or an empty path (which every caller treats like null).
std::optional<std::string> RefPath(const Value* value);

// Game object paths. The package "/Game/X/Y" is the file "Astro/Content/X/Y.uasset"
// (+ ".uexp") in the pak.
std::string PackageToPakPath(std::string_view packagePath);  // throws std::runtime_error outside /Game/
std::string PakPathToPackage(std::string_view pakPath);      // throws std::runtime_error outside Astro/Content/
// "/Game/A/B.B_C" -> "/Game/A/B"
std::string PackageOf(std::string_view objectPath);
// The part after the last ".", without a trailing "_C":
// "/Game/Components_Small/Splitter/Splitter_3Way.Splitter_3Way_C" -> "Splitter_3Way"
std::string ObjectName(std::string_view objectPath);

}  // namespace rd::extract
