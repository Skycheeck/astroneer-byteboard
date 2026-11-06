// Reads files out of an Unreal Engine 4.27 pak archive (pak version 11) through its full directory index.
// Input: the .pak file. Output: openPak() → {files, read(path) → Buffer, close()}, where paths are
// relative to the mount point, for example "Astro/Content/Items/ItemTypes/Components/Generator_Small.uasset".
// Supported: an unencrypted index, uncompressed and Zlib-compressed entries, no entry encryption. This is what
// ASTRONEER ships; anything else is refused with a FormatError instead of being misread.
// Layouts follow UE 4.27 FPakInfo, FPakFile::LoadIndexInternal and FPakFile::DecodePakEntry.
import fs from 'node:fs';
import zlib from 'node:zlib';
import {FormatError, Reader} from './binreader.mjs';

const PAK_MAGIC = 0x5A6F12E1;
const PAK_VERSION = 11;

// FPakInfo as serialized at the end of the file for version 11 with 5 compression method names.
const FOOTER_SIZE = 221;
const FOOTER_ENCRYPTED_INDEX = 16;   // u8 bEncryptedIndex, after the 16-byte EncryptionKeyGuid
const FOOTER_MAGIC = 17;             // u32
const FOOTER_VERSION = 21;           // i32
const FOOTER_INDEX_OFFSET = 25;      // i64
const FOOTER_INDEX_SIZE = 33;        // i64, followed by the 20-byte index SHA-1
const FOOTER_COMPRESSION_NAMES = 61; // 5 × 32-byte NUL-padded ANSI names
const COMPRESSION_NAME_SIZE = 32;
const COMPRESSION_NAME_COUNT = 5;

const SHA1_SIZE = 20;
// Offset (i64) + size (i64) + SHA-1 of a secondary index, stored when its "has index" flag is set.
const SECONDARY_INDEX_REF_SIZE = 8 + 8 + SHA1_SIZE;

// Serialized FPakEntry in front of every file's data: offset, size, uncompressed size (3 × i64), compression
// method (u32), SHA-1, flags (u8) and compression block size (u32). A compressed entry also stores its block
// table (i32 count + count × {i64 start, i64 end}) between the SHA-1 and the flags.
const ENTRY_HEADER_SIZE = 53;
const BLOCK_TABLE_COUNT_SIZE = 4;
const BLOCK_TABLE_ENTRY_SIZE = 16;

export function openPak(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const fileSize = fs.fstatSync(fd).size;
    const readAt = (offset, length) => readExactly(fd, offset, length, fileSize);
    const footer = readFooter(readAt, fileSize, file);
    const index = new Reader(readAt(footer.indexOffset, footer.indexSize));
    const primary = readPrimaryIndex(index);
    const directory = new Reader(readAt(primary.directoryIndexOffset, primary.directoryIndexSize));
    const files = readDirectoryIndex(directory);
    const encodedEntries = primary.encodedEntries;

    function read(path) {
      const location = files.get(path);
      if (location === undefined) throw new FormatError(`not in the pak: ${path}`);
      const entry = decodeEntry(encodedEntries, location, path);
      if (entry.encrypted) throw new FormatError(`encrypted pak entry is not supported: ${path}`);
      if (entry.compressionMethod === 0) {
        return readAt(entry.offset + ENTRY_HEADER_SIZE, entry.uncompressedSize);
      }
      const methodName = footer.compressionNames[entry.compressionMethod - 1];
      if (methodName !== 'Zlib') {
        throw new FormatError(`compression method ${methodName ?? entry.compressionMethod} is not supported: ${path}`);
      }
      const blockTableSize = BLOCK_TABLE_COUNT_SIZE + BLOCK_TABLE_ENTRY_SIZE * entry.blockSizes.length;
      let position = entry.offset + ENTRY_HEADER_SIZE + blockTableSize;
      const parts = [];
      for (const blockSize of entry.blockSizes) {
        parts.push(zlib.inflateSync(readAt(position, blockSize)));
        position += blockSize;
      }
      const data = Buffer.concat(parts);
      if (data.length !== entry.uncompressedSize) {
        throw new FormatError(`${path} inflated to ${data.length} bytes instead of ${entry.uncompressedSize}`);
      }
      return data;
    }

    function close() {
      fs.closeSync(fd);
    }

    return {files, read, close};
  } catch (error) {
    fs.closeSync(fd);
    throw error;
  }
}

function readExactly(fd, offset, length, fileSize) {
  if (offset < 0 || length < 0 || offset + length > fileSize) {
    throw new FormatError(`pak read of ${length} bytes at ${offset} is outside the ${fileSize}-byte file`);
  }
  const buffer = Buffer.alloc(length);
  let done = 0;
  while (done < length) {
    const count = fs.readSync(fd, buffer, done, length - done, offset + done);
    if (count === 0) throw new FormatError(`pak ended early at ${offset + done}`);
    done += count;
  }
  return buffer;
}

function readFooter(readAt, fileSize, file) {
  if (fileSize < FOOTER_SIZE) throw new FormatError(`not a v${PAK_VERSION} pak: ${file}`);
  const footer = readAt(fileSize - FOOTER_SIZE, FOOTER_SIZE);
  const magic = footer.readUInt32LE(FOOTER_MAGIC);
  const version = footer.readInt32LE(FOOTER_VERSION);
  if (magic !== PAK_MAGIC || version !== PAK_VERSION) throw new FormatError(`not a v${PAK_VERSION} pak: ${file}`);
  if (footer[FOOTER_ENCRYPTED_INDEX] !== 0) throw new FormatError(`encrypted pak index is not supported: ${file}`);
  const reader = new Reader(footer, FOOTER_INDEX_OFFSET);
  const indexOffset = reader.i64();
  const indexSize = reader.i64();
  const compressionNames = [];
  for (let k = 0; k < COMPRESSION_NAME_COUNT; k++) {
    const start = FOOTER_COMPRESSION_NAMES + k * COMPRESSION_NAME_SIZE;
    const raw = footer.toString('latin1', start, start + COMPRESSION_NAME_SIZE);
    compressionNames.push(raw.replace(/\0.*$/s, ''));
  }
  return {indexOffset, indexSize, compressionNames};
}

// The primary index: mount point, entry count, path hash seed, references to the path hash index and the full
// directory index, then the encoded entries that directory locations point into.
function readPrimaryIndex(reader) {
  reader.fstring();  // mount point
  reader.i32();      // entry count
  reader.u64();      // path hash seed
  const hasPathHashIndex = reader.u32();
  if (hasPathHashIndex) reader.skip(SECONDARY_INDEX_REF_SIZE);
  const hasDirectoryIndex = reader.u32();
  if (!hasDirectoryIndex) throw new FormatError('pak has no full directory index');
  const directoryIndexOffset = reader.i64();
  const directoryIndexSize = reader.i64();
  reader.skip(SHA1_SIZE);
  const encodedEntriesSize = reader.i32();
  const encodedEntries = reader.bytes(encodedEntriesSize);
  return {directoryIndexOffset, directoryIndexSize, encodedEntries};
}

// The full directory index: for each directory, its name and its files' names with their entry locations.
function readDirectoryIndex(reader) {
  const files = new Map();
  const directoryCount = reader.u32();
  for (let d = 0; d < directoryCount; d++) {
    const directory = reader.fstring();
    const fileCount = reader.u32();
    for (let f = 0; f < fileCount; f++) {
      const name = reader.fstring();
      const location = reader.i32();
      files.set((directory + name).replace(/^\//, ''), location);
    }
  }
  return files;
}

// An encoded entry starts with a u32 bitfield:
//   bit 31      offset fits in 32 bits (else 64)
//   bit 30      uncompressed size fits in 32 bits
//   bit 29      compressed size fits in 32 bits
//   bits 28-23  compression method (0 = none, else 1-based index into the footer names)
//   bit 22      encrypted
//   bits 21-6   compression block count
//   bits 5-0    compression block size in 2 KB units; 0x3f means a u32 with the size follows
// then offset, uncompressed size, compressed size (only when compressed) and, unless there is exactly one
// unencrypted block (whose size is the compressed size), one u32 size per block.
function decodeEntry(encodedEntries, location, path) {
  if (location < 0) throw new FormatError(`non-encoded pak entry is not supported: ${path}`);
  const reader = new Reader(encodedEntries, location);
  const bits = reader.u32();
  if ((bits & 0x3f) === 0x3f) reader.u32();  // block size, not needed to read the blocks
  const offsetIs32Bit = (bits >>> 31) & 1;
  const uncompressedSizeIs32Bit = (bits >>> 30) & 1;
  const sizeIs32Bit = (bits >>> 29) & 1;
  const compressionMethod = (bits >>> 23) & 0x3f;
  const encrypted = ((bits >>> 22) & 1) === 1;
  const blockCount = (bits >>> 6) & 0xffff;

  const readVarSize = is32Bit => (is32Bit ? reader.u32() : reader.u64());
  const offset = readVarSize(offsetIs32Bit);
  const uncompressedSize = readVarSize(uncompressedSizeIs32Bit);
  const size = compressionMethod ? readVarSize(sizeIs32Bit) : uncompressedSize;

  const blockSizes = [];
  if (blockCount === 1 && !encrypted) {
    blockSizes.push(size);
  } else {
    for (let k = 0; k < blockCount; k++) blockSizes.push(reader.u32());
  }
  return {offset, size, uncompressedSize, compressionMethod, encrypted, blockSizes};
}
