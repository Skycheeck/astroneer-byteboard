// The icon atlas the in-game overlay reads, overlay/icons.rgba, as docs/PROTOCOL.md fixes it: u32 width and
// u32 height (little-endian), then RGBA8 pixels row by row; ICON_SIZE×ICON_SIZE cells, ICONS_PER_ROW per row,
// in icon index order. The icon steps produce every icon at ICON_SIZE so that it fits a cell.
export const ICON_SIZE = 64;
const ICONS_PER_ROW = 16;
const HEADER_SIZE = 8;
const BYTES_PER_PIXEL = 4;

// pixelsAt(index) returns the RGBA8 pixels of icon `index`, ICON_SIZE×ICON_SIZE.
export function buildAtlas(iconCount, pixelsAt) {
  const width = ICONS_PER_ROW * ICON_SIZE;
  const height = Math.ceil(iconCount / ICONS_PER_ROW) * ICON_SIZE;
  const atlas = Buffer.alloc(HEADER_SIZE + width * height * BYTES_PER_PIXEL);
  atlas.writeUInt32LE(width, 0);
  atlas.writeUInt32LE(height, 4);
  const iconRowSize = ICON_SIZE * BYTES_PER_PIXEL;
  for (let index = 0; index < iconCount; index++) {
    const pixels = pixelsAt(index);
    const left = (index % ICONS_PER_ROW) * ICON_SIZE;
    const top = Math.floor(index / ICONS_PER_ROW) * ICON_SIZE;
    for (let y = 0; y < ICON_SIZE; y++) {
      const target = HEADER_SIZE + ((top + y) * width + left) * BYTES_PER_PIXEL;
      pixels.copy(atlas, target, y * iconRowSize, (y + 1) * iconRowSize);
    }
  }
  return {atlas, width, height};
}
