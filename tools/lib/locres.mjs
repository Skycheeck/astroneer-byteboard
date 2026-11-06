// Reads a compiled localization file (.locres) as UE 4.27 writes it (FTextLocalizationResource::LoadFromArchive).
// Input: the file's bytes. Output: {namespace: {key: localized string}}.
// Supported versions: 2 (Optimized_CRC32) and 3 (Optimized_CityHash64_UTF16), the formats UE 4.20+ writes.
import {FormatError, Reader} from './binreader.mjs';

// FGuid(0x7574140E, 0xFC034A67, 0x9D90154A, 0x1B7F37C3), stored as four little-endian uint32.
const LOCRES_MAGIC = Buffer.from(['0e147475', '674a03fc', '4a15909d', 'c3377f1b'].join(''), 'hex');
const VERSION_OPTIMIZED_CRC32 = 2;
const VERSION_OPTIMIZED_CITYHASH64 = 3;
const NO_STRING_ARRAY = -1;

export function parseLocres(buffer) {
  if (buffer.length < LOCRES_MAGIC.length || !buffer.subarray(0, LOCRES_MAGIC.length).equals(LOCRES_MAGIC)) {
    throw new FormatError('not a .locres file, or a legacy one without the magic number');
  }
  const reader = new Reader(buffer, LOCRES_MAGIC.length);
  const version = reader.u8();
  if (version !== VERSION_OPTIMIZED_CRC32 && version !== VERSION_OPTIMIZED_CITYHASH64) {
    throw new FormatError(`unsupported .locres version ${version}`);
  }
  const stringArrayOffset = reader.i64();
  reader.u32();  // entry count
  const strings = stringArrayOffset === NO_STRING_ARRAY ? [] : readStringArray(buffer, stringArrayOffset);

  const out = {};
  const namespaceCount = reader.u32();
  for (let n = 0; n < namespaceCount; n++) {
    reader.u32();  // namespace hash
    const namespace = reader.fstring();
    const entries = out[namespace] ??= {};
    const keyCount = reader.u32();
    for (let k = 0; k < keyCount; k++) {
      reader.u32();  // key hash
      const key = reader.fstring();
      reader.u32();  // source string hash
      const stringIndex = reader.i32();
      entries[key] = strings[stringIndex];
    }
  }
  return out;
}

// The localized strings, each followed by its reference count.
function readStringArray(buffer, offset) {
  const reader = new Reader(buffer, offset);
  const count = reader.i32();
  const strings = [];
  for (let k = 0; k < count; k++) {
    strings.push(reader.fstring());
    reader.i32();  // reference count
  }
  return strings;
}
