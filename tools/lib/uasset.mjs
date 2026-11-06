// Parses a cooked, unversioned UE 4.27 package: the .uasset header (package summary, name, import and export
// tables) and the tagged properties of every export, which a cooked split package keeps in the .uexp.
// Input: parseAsset(uasset, uexp, packagePath), with packagePath like "/Game/Items/ItemTypes/Components/Zinc".
// Output: {names, exports, warnings}. Each export is
//   {name, path, classPath, className, superPath, props, dataStart, dataEnd, propertiesEnd}
// where dataStart/dataEnd delimit the export in the .uexp and propertiesEnd is where its tagged properties end
// (native serialization, such as a string table or texture data, follows there).
//
// Property values in props:
//   Int, Float (rounded to 3 decimals), Bool, Name and Str properties: plain JSON values
//   Object and Class properties: "@" + full object path, e.g. "@/Game/Items/ItemTypes/Minables/Zinc.Zinc_C";
//     null for a null reference (in a cooked package that means the value really is None)
//   SoftObjectProperty: "~" + asset path + sub-path
//   Byte and Enum properties: the enum value name, e.g. "EItemVariantType::Engineer", or the number of a plain byte
//   TextProperty: {key, src} for a base text ({ns, key, src} when its namespace is not empty), {table, key} for a
//     string table entry, or the string of a culture-invariant text
//   StructProperty: an object of the struct's tagged properties, or "<Name>" for a natively serialized struct
//   ArrayProperty: an array of such values; ["<Type xN>"] for element types this reader does not decode
//   any other property type: "<Type Nb>"
// A property that cannot be decoded is left out and described in warnings.
// Also exported for readers of the native data after the properties: readName, skipObjectGuid, and refPath, which
// turns an Object property value back into its path.
import {FormatError, Reader} from './binreader.mjs';

const PACKAGE_TAG = 0x9E2A83C1;
const LEGACY_FILE_VERSION_UE4_27 = -7;
const PKG_FILTER_EDITOR_ONLY = 0x80000000;

const GUID_SIZE = 16;
const CUSTOM_VERSION_SIZE = GUID_SIZE + 4;  // FCustomVersion: key GUID + int32 version
const NAME_HASHES_SIZE = 4;                 // two uint16 hashes after each name in the name table
const FNAME_SIZE = 8;                       // name index + number
const IMPORT_SIZE = 28;                     // FObjectImport
const IMPORT_OUTER_INDEX = 2 * FNAME_SIZE;  // after ClassPackage and ClassName
const EXPORT_SIZE = 104;                    // FObjectExport as UE 4.27 writes it in cooked packages
const MAX_OUTER_DEPTH = 32;

const TEXT_HISTORY_NONE = -1;
const TEXT_HISTORY_BASE = 0;
const TEXT_HISTORY_STRING_TABLE_ENTRY = 11;

// Structs whose tagged-property value is written by the struct's own Serialize, not as tagged properties.
const NATIVE_STRUCTS = new Set([
  'Box', 'Box2D', 'Color', 'DateTime', 'FrameNumber', 'GameplayTagContainer', 'Guid', 'IntPoint', 'IntVector',
  'LinearColor', 'Quat', 'Rotator', 'SoftClassPath', 'SoftObjectPath', 'Timespan', 'Vector', 'Vector2D', 'Vector4',
]);

export function parseAsset(uasset, uexp, packagePath) {
  const header = new Reader(uasset);
  const summary = readSummary(header);
  const names = readNames(header, summary);
  const imports = readImports(header, summary, names);
  const exportEntries = readExports(header, summary, names);
  const pathOf = makePathResolver(imports, exportEntries, packagePath);
  const warnings = [];

  const exports = exportEntries.map((entry, k) => {
    const dataStart = entry.serialOffset - summary.totalHeaderSize;
    const dataEnd = dataStart + entry.serialSize;
    const classPath = pathOf(entry.classIndex);
    const result = {
      name: entry.name,
      path: pathOf(k + 1),
      classPath,
      className: classPath === null ? null : leafName(classPath),
      superPath: pathOf(entry.superIndex),
      props: {},
      dataStart,
      dataEnd,
      propertiesEnd: null,
    };
    const scope = {names, pathOf, warnings, where: entry.name};
    try {
      if (dataStart < 0 || dataEnd > uexp.length) throw new FormatError('export data is outside the .uexp');
      const reader = new Reader(uexp, dataStart);
      result.props = readProperties(reader, dataEnd, scope);
      result.propertiesEnd = reader.position;
    } catch (error) {
      warnings.push(`${entry.name}: ${error.message}`);
    }
    return result;
  });

  return {names, exports, warnings};
}

// FPackageFileSummary up to the import table offset.
function readSummary(reader) {
  const tag = reader.u32();
  if (tag !== PACKAGE_TAG) throw new FormatError('not a UE package (wrong tag)');
  const legacyFileVersion = reader.i32();
  if (legacyFileVersion !== LEGACY_FILE_VERSION_UE4_27) {
    throw new FormatError(`package version ${legacyFileVersion} is not UE 4.27 (${LEGACY_FILE_VERSION_UE4_27})`);
  }
  reader.i32();  // LegacyUE3Version
  reader.i32();  // FileVersionUE4: 0 in unversioned packages, which use the engine's own version
  reader.i32();  // FileVersionLicenseeUE4
  const customVersionCount = reader.i32();
  reader.skip(customVersionCount * CUSTOM_VERSION_SIZE);
  const totalHeaderSize = reader.i32();
  reader.fstring();  // FolderName
  const packageFlags = reader.u32();
  const nameCount = reader.i32();
  const nameOffset = reader.i32();
  if (!(packageFlags & PKG_FILTER_EDITOR_ONLY)) reader.fstring();  // LocalizationId, editor packages only
  reader.i32();  // GatherableTextDataCount
  reader.i32();  // GatherableTextDataOffset
  const exportCount = reader.i32();
  const exportOffset = reader.i32();
  const importCount = reader.i32();
  const importOffset = reader.i32();
  return {totalHeaderSize, nameCount, nameOffset, exportCount, exportOffset, importCount, importOffset};
}

function readNames(reader, summary) {
  reader.seek(summary.nameOffset);
  const names = [];
  for (let k = 0; k < summary.nameCount; k++) {
    names.push(reader.fstring());
    reader.skip(NAME_HASHES_SIZE);
  }
  return names;
}

// An FName is an index into the name table plus a number; number N > 0 stands for the suffix "_<N-1>".
export function readName(reader, names) {
  const index = reader.i32();
  const number = reader.i32();
  const name = names[index];
  if (name === undefined) throw new FormatError(`name index ${index} is out of range`);
  return number ? `${name}_${number - 1}` : name;
}

// Only what object paths are built from: the outer and the name. The class of an import is not needed.
function readImports(reader, summary, names) {
  const imports = [];
  for (let k = 0; k < summary.importCount; k++) {
    reader.seek(summary.importOffset + k * IMPORT_SIZE + IMPORT_OUTER_INDEX);
    const outerIndex = reader.i32();
    const name = readName(reader, names);
    imports.push({outerIndex, name});
  }
  return imports;
}

function readExports(reader, summary, names) {
  const exports = [];
  for (let k = 0; k < summary.exportCount; k++) {
    reader.seek(summary.exportOffset + k * EXPORT_SIZE);
    const classIndex = reader.i32();
    const superIndex = reader.i32();
    reader.i32();  // TemplateIndex
    const outerIndex = reader.i32();
    const name = readName(reader, names);
    reader.u32();  // ObjectFlags
    const serialSize = reader.i64();
    const serialOffset = reader.i64();
    // The rest of the record (export flags, package GUID and flags, dependency indices) is not needed.
    exports.push({classIndex, superIndex, outerIndex, name, serialSize, serialOffset});
  }
  return exports;
}

// Object index → full object path: 0 is null, -1 - k is import k, k + 1 is export k. A path is
// "<package>.<object>", with ":" before a subobject of a top-level object and "." below that (UObject::GetPathName).
function makePathResolver(imports, exports, packagePath) {
  const cache = new Map();

  function chainOf(index, depth) {
    if (depth > MAX_OUTER_DEPTH) throw new FormatError(`outer chain of object ${index} is too deep`);
    const object = index < 0 ? imports[-index - 1] : exports[index - 1];
    if (!object) throw new FormatError(`object index ${index} is out of range`);
    if (object.outerIndex === 0) return index < 0 ? [object.name] : [packagePath, object.name];
    return [...chainOf(object.outerIndex, depth + 1), object.name];
  }

  return function pathOf(index) {
    if (index === 0) return null;
    if (!cache.has(index)) {
      const chain = chainOf(index, 0);
      let path = chain[0];
      for (let k = 1; k < chain.length; k++) path += (k === 2 ? ':' : '.') + chain[k];
      cache.set(index, path);
    }
    return cache.get(index);
  };
}

function leafName(path) {
  return path.slice(Math.max(path.lastIndexOf('.'), path.lastIndexOf(':')) + 1);
}

// Tagged properties up to the "None" name or `end`. The reader is left after "None".
function readProperties(reader, end, scope) {
  const out = {};
  while (reader.position < end) {
    const tag = readTag(reader, scope.names);
    if (tag === null) break;
    const valueStart = reader.position;
    const valueEnd = valueStart + tag.size;
    try {
      const value = readValue(reader, tag, valueEnd, scope);
      out[tag.arrayIndex ? `${tag.name}[${tag.arrayIndex}]` : tag.name] = value;
    } catch (error) {
      scope.warnings.push(`${scope.where}: ${tag.name} (${tag.type}): ${error.message}`);
    }
    reader.seek(valueEnd);
  }
  return out;
}

// FPropertyTag: name, type, value size, array index, the type's extra fields, optional property GUID.
function readTag(reader, names) {
  const name = readName(reader, names);
  if (name === 'None') return null;
  const type = readName(reader, names);
  const size = reader.i32();
  const arrayIndex = reader.i32();
  const tag = {name, type, size, arrayIndex};
  switch (type) {
    case 'StructProperty':
      tag.structName = readName(reader, names);
      reader.skip(GUID_SIZE);
      break;
    case 'BoolProperty':
      tag.boolValue = reader.u8() !== 0;
      break;
    case 'ByteProperty':
    case 'EnumProperty':
      tag.enumName = readName(reader, names);
      break;
    case 'ArrayProperty':
    case 'SetProperty':
      tag.innerType = readName(reader, names);
      break;
    case 'MapProperty':
      tag.innerType = readName(reader, names);
      tag.valueType = readName(reader, names);
      break;
  }
  const hasPropertyGuid = reader.u8();
  if (hasPropertyGuid) reader.skip(GUID_SIZE);
  return tag;
}

function readValue(reader, tag, valueEnd, scope) {
  switch (tag.type) {
    case 'IntProperty':
      return reader.i32();
    case 'FloatProperty':
      return roundFloat(reader.f32());
    case 'BoolProperty':
      return tag.boolValue;
    case 'NameProperty':
      return readName(reader, scope.names);
    case 'StrProperty':
      return reader.fstring();
    case 'ObjectProperty':
    case 'ClassProperty':
      return objectRef(scope.pathOf(reader.i32()));
    case 'SoftObjectProperty':
      return readSoftObjectRef(reader, scope);
    case 'ByteProperty':
    case 'EnumProperty':
      return tag.enumName === 'None' && tag.size === 1 ? reader.u8() : readName(reader, scope.names);
    case 'TextProperty':
      return readText(reader, scope);
    case 'StructProperty':
      if (NATIVE_STRUCTS.has(tag.structName)) return `<${tag.structName}>`;
      return readProperties(reader, valueEnd, scope);
    case 'ArrayProperty':
      return readArray(reader, tag, valueEnd, scope);
    default:
      return `<${tag.type} ${tag.size}b>`;
  }
}

function roundFloat(value) {
  return +value.toFixed(3);
}

function objectRef(path) {
  return path === null ? null : '@' + path;
}

// The object path of an Object/Class property value, or null for a null reference or any other value.
export function refPath(value) {
  return typeof value === 'string' && value.startsWith('@') ? value.slice(1) : null;
}

function readSoftObjectRef(reader, scope) {
  const assetPath = readName(reader, scope.names);
  const subPath = reader.fstring();
  return '~' + assetPath + subPath;
}

// FText: flags, history type, then the history's own fields.
function readText(reader, scope) {
  reader.u32();  // flags
  const historyType = reader.i8();
  switch (historyType) {
    case TEXT_HISTORY_NONE: {
      const hasCultureInvariantString = reader.i32();
      return hasCultureInvariantString ? reader.fstring() : '';
    }
    case TEXT_HISTORY_BASE: {
      const ns = reader.fstring();
      const key = reader.fstring();
      const src = reader.fstring();
      return ns ? {ns, key, src} : {key, src};
    }
    case TEXT_HISTORY_STRING_TABLE_ENTRY: {
      const table = readName(reader, scope.names);
      const key = reader.fstring();
      return {table, key};
    }
    default:
      throw new FormatError(`text history type ${historyType} is not supported`);
  }
}

function readArray(reader, tag, valueEnd, scope) {
  const count = reader.i32();
  const values = [];
  switch (tag.innerType) {
    case 'ObjectProperty':
      for (let k = 0; k < count; k++) values.push(objectRef(scope.pathOf(reader.i32())));
      return values;
    case 'IntProperty':
      for (let k = 0; k < count; k++) values.push(reader.i32());
      return values;
    case 'NameProperty':
    case 'EnumProperty':
      for (let k = 0; k < count; k++) values.push(readName(reader, scope.names));
      return values;
    case 'ByteProperty':
      // An enum byte array stores FNames (8 bytes each); a plain byte array is not decoded.
      if (tag.size !== 4 + count * 8) return [`<${tag.innerType} x${count}>`];
      for (let k = 0; k < count; k++) values.push(readName(reader, scope.names));
      return values;
    case 'SoftObjectProperty':
      for (let k = 0; k < count; k++) values.push(readSoftObjectRef(reader, scope));
      return values;
    case 'StructProperty':
      return readStructArray(reader, count, valueEnd, scope);
    default:
      return [`<${tag.innerType} x${count}>`];
  }
}

// A struct array repeats a full property tag for its elements (name, type, size, array index, struct name,
// struct GUID, property GUID flag), then holds the elements one after another.
function readStructArray(reader, count, valueEnd, scope) {
  readName(reader, scope.names);  // element tag name
  readName(reader, scope.names);  // element tag type, StructProperty
  reader.i32();                   // size of all elements
  reader.i32();                   // array index
  const structName = readName(reader, scope.names);
  reader.skip(GUID_SIZE);
  const hasPropertyGuid = reader.u8();
  if (hasPropertyGuid) reader.skip(GUID_SIZE);
  const values = [];
  for (let k = 0; k < count; k++) {
    values.push(NATIVE_STRUCTS.has(structName) ? `<${structName}>` : readProperties(reader, valueEnd, scope));
  }
  return values;
}

// UObject::Serialize ends with FLazyObjectPtr::PossiblySerializeObjectGuid: a bool (4 bytes) and, when it is set,
// the object's GUID. Native data of a string table or texture follows it.
export function skipObjectGuid(reader) {
  const hasGuid = reader.u32();
  if (hasGuid) reader.skip(GUID_SIZE);
}
