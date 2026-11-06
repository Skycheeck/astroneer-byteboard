// Minimal PNG writer and reader for the icons: 8-bit RGBA, not interlaced, every row with filter 0 (none).
// encodePng writes exactly that; decodePng reads only that, which is all the tools ever produce.
import zlib from 'node:zlib';
import {FormatError} from './binreader.mjs';

const SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
const BIT_DEPTH = 8;
const COLOR_TYPE_RGBA = 6;
const FILTER_NONE = 0;
const DATA_URI_PREFIX = 'data:image/png;base64,';

const CRC_TABLE = new Int32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c;
});

function crc32(bytes) {
  let c = -1;
  for (const byte of bytes) c = CRC_TABLE[(c ^ byte) & 255] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}

function chunk(type, data) {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const typeAndData = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(typeAndData));
  return Buffer.concat([length, typeAndData, crc]);
}

export function encodePng(pixels, width, height) {
  const rowSize = width * 4;
  const raw = Buffer.alloc((rowSize + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (rowSize + 1)] = FILTER_NONE;
    pixels.copy(raw, y * (rowSize + 1) + 1, y * rowSize, (y + 1) * rowSize);
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = BIT_DEPTH;
  header[9] = COLOR_TYPE_RGBA;
  // Bytes 10-12: compression, filter and interlace methods, all 0.
  return Buffer.concat([
    SIGNATURE,
    chunk('IHDR', header),
    chunk('IDAT', zlib.deflateSync(raw, {level: 9})),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

export function decodePng(png) {
  if (png.length < SIGNATURE.length || !png.subarray(0, SIGNATURE.length).equals(SIGNATURE)) {
    throw new FormatError('not a PNG');
  }
  let width = 0;
  let height = 0;
  const idat = [];
  for (let position = SIGNATURE.length; position + 8 <= png.length;) {
    const length = png.readUInt32BE(position);
    const type = png.toString('latin1', position + 4, position + 8);
    const data = png.subarray(position + 8, position + 8 + length);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      if (data[8] !== BIT_DEPTH || data[9] !== COLOR_TYPE_RGBA || data[12] !== 0) {
        throw new FormatError('PNG is not 8-bit RGBA without interlacing');
      }
    } else if (type === 'IDAT') {
      idat.push(data);
    }
    position += 12 + length;
  }
  const rowSize = width * 4;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  if (raw.length !== (rowSize + 1) * height) throw new FormatError('PNG image data has the wrong size');
  const pixels = Buffer.alloc(rowSize * height);
  for (let y = 0; y < height; y++) {
    const rowStart = y * (rowSize + 1);
    if (raw[rowStart] !== FILTER_NONE) throw new FormatError('PNG row uses a filter');
    raw.copy(pixels, y * rowSize, rowStart + 1, rowStart + 1 + rowSize);
  }
  return {width, height, pixels};
}

export function toDataUri(png) {
  return DATA_URI_PREFIX + png.toString('base64');
}

export function fromDataUri(uri) {
  if (!uri.startsWith(DATA_URI_PREFIX)) throw new FormatError('not a PNG data URI');
  return Buffer.from(uri.slice(DATA_URI_PREFIX.length), 'base64');
}
