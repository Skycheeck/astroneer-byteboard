// Reads the top mip of a cooked UE 4.27 Texture2D whose pixels are stored inline in the .uexp.
// Input: the pak and the texture's object path,
//   e.g. "/Game/UI/Textures/Icons/Items/ui_icon_comp_tapper.ui_icon_comp_tapper".
// Output: {width, height, pixelFormat, data}, with data the mip's raw bytes in that pixel format.
// Layout after the export's tagged properties (UTexture2D::Serialize, UTexture::SerializeCookedPlatformData,
// FTexturePlatformData::SerializeCooked, FTexture2DMipMap::Serialize, FUntypedBulkData::Serialize).
import {FormatError, Reader} from './binreader.mjs';
import {packageOf, packageToPakPath} from './paths.mjs';
import {parseAsset, readName, skipObjectGuid} from './uasset.mjs';

const STRIP_DATA_FLAGS_SIZE = 2;          // FStripDataFlags: global and class strip flags
const PACKED_DATA_HAS_OPT_DATA = 1 << 30; // FTexturePlatformData::PackedData bit
const OPT_DATA_SIZE = 8;                  // FOptTexturePlatformData: ExtData, NumMipsInTail

const BULKDATA_PAYLOAD_AT_END_OF_FILE = 0x0001;
const BULKDATA_SERIALIZE_COMPRESSED_ZLIB = 0x0002;
const BULKDATA_UNUSED = 0x0020;
const BULKDATA_PAYLOAD_IN_SEPARATE_FILE = 0x0100;
const BULKDATA_SERIALIZE_COMPRESSED_BIT_WINDOW = 0x0200;
const BULKDATA_OPTIONAL_PAYLOAD = 0x0800;
const BULKDATA_SIZE_64BIT = 0x2000;
const BULKDATA_NOT_INLINE = BULKDATA_PAYLOAD_AT_END_OF_FILE | BULKDATA_SERIALIZE_COMPRESSED_ZLIB | BULKDATA_UNUSED
  | BULKDATA_PAYLOAD_IN_SEPARATE_FILE | BULKDATA_SERIALIZE_COMPRESSED_BIT_WINDOW | BULKDATA_OPTIONAL_PAYLOAD;

export function readTexture2D(pak, objectPath) {
  const packagePath = packageOf(objectPath);
  const file = packageToPakPath(packagePath);
  const uexp = pak.read(file + '.uexp');
  const asset = parseAsset(pak.read(file + '.uasset'), uexp, packagePath);
  const texture = asset.exports.find(e => e.path === objectPath);
  if (!texture || texture.className !== 'Texture2D') throw new FormatError(`no Texture2D ${objectPath}`);
  if (texture.propertiesEnd === null) throw new FormatError(`unreadable texture properties in ${file}`);

  const reader = new Reader(uexp, texture.propertiesEnd);
  skipObjectGuid(reader);
  reader.skip(STRIP_DATA_FLAGS_SIZE);  // UTexture
  reader.skip(STRIP_DATA_FLAGS_SIZE);  // UTexture2D
  const cooked = reader.u32();
  if (!cooked) throw new FormatError('texture is not cooked');
  const pixelFormatName = readName(reader, asset.names);
  reader.i64();  // where the next pixel format's data starts

  // FTexturePlatformData
  reader.i32();  // SizeX of the full texture
  reader.i32();  // SizeY
  const packedData = reader.u32();
  const pixelFormat = reader.fstring();
  if (pixelFormat !== pixelFormatName) {
    throw new FormatError(`texture header misread (${pixelFormatName}, ${pixelFormat})`);
  }
  if (packedData & PACKED_DATA_HAS_OPT_DATA) reader.skip(OPT_DATA_SIZE);
  reader.i32();  // FirstMipToSerialize
  const mipCount = reader.i32();
  if (mipCount < 1) throw new FormatError('texture has no mips');

  // The first serialized mip: bCooked, then its bulk data header and payload, then its size.
  reader.u32();
  const bulkFlags = reader.u32();
  if (bulkFlags & BULKDATA_NOT_INLINE) {
    throw new FormatError(`texture mip is not stored inline (bulk data flags 0x${bulkFlags.toString(16)})`);
  }
  const is64Bit = (bulkFlags & BULKDATA_SIZE_64BIT) !== 0;
  const elementCount = is64Bit ? reader.i64() : reader.i32();
  const sizeOnDisk = is64Bit ? reader.i64() : reader.i32();
  reader.i64();  // OffsetInFile
  if (elementCount <= 0 || sizeOnDisk !== elementCount) throw new FormatError('texture mip has no plain inline data');
  const data = reader.bytes(elementCount);
  const width = reader.i32();
  const height = reader.i32();
  return {width, height, pixelFormat, data};
}
