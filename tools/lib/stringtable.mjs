// Reads a cooked string table asset (UStringTable, UE 4.27 FStringTable::Serialize).
// Input: the pak and the table's object path from a TextProperty, e.g. "/Game/Globals/StringTables/Items.Items".
// Output: {namespace, entries: {key: source string}}. The localized text of an entry is the .locres string
// under that namespace and key; the source string is the fallback.
import {FormatError, Reader} from './binreader.mjs';
import {packageOf, packageToPakPath} from './paths.mjs';
import {parseAsset, skipObjectGuid} from './uasset.mjs';

export function readStringTable(pak, tablePath) {
  const packagePath = packageOf(tablePath);
  const file = packageToPakPath(packagePath);
  const uexp = pak.read(file + '.uexp');
  const asset = parseAsset(pak.read(file + '.uasset'), uexp, packagePath);
  const table = asset.exports.find(e => e.className === 'StringTable');
  if (!table || table.propertiesEnd === null) throw new FormatError(`no readable StringTable export in ${file}`);

  // After the object's tagged properties: the object GUID, then TableNamespace, the entry count and the
  // key/source string pairs (the entries' metadata follows and is not needed).
  const reader = new Reader(uexp, table.propertiesEnd);
  skipObjectGuid(reader);
  const namespace = reader.fstring();
  const count = reader.i32();
  const entries = {};
  for (let k = 0; k < count; k++) {
    const key = reader.fstring();
    entries[key] = reader.fstring();
  }
  return {namespace, entries};
}
