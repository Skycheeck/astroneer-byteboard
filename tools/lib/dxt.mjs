// Pixel decoding for the texture formats ASTRONEER's icons use, and a box-filter resize.
// Input: a texture mip from lib/texture.mjs ({width, height, pixelFormat, data}).
// Output: RGBA8 pixels, row by row. DXT1 and DXT5 are BC1 and BC3 block compression: 4×4 texel blocks,
// 8 bytes of colour (BC1) preceded in BC3 by 8 bytes of alpha.
import {FormatError} from './binreader.mjs';

const BLOCK_TEXELS = 4;
const BC1_BLOCK_SIZE = 8;
const BC3_BLOCK_SIZE = 16;

export function decodeToRgba(texture) {
  const {width, height, pixelFormat, data} = texture;
  switch (pixelFormat) {
    case 'PF_DXT1':
      return decodeBlocks(data, width, height, false);
    case 'PF_DXT5':
      return decodeBlocks(data, width, height, true);
    case 'PF_B8G8R8A8':
      return bgraToRgba(data, width, height);
    default:
      throw new FormatError(`unsupported pixel format ${pixelFormat}`);
  }
}

function decodeBlocks(data, width, height, hasAlphaBlock) {
  const blockSize = hasAlphaBlock ? BC3_BLOCK_SIZE : BC1_BLOCK_SIZE;
  const blocksWide = Math.ceil(width / BLOCK_TEXELS);
  const blocksHigh = Math.ceil(height / BLOCK_TEXELS);
  if (data.length < blocksWide * blocksHigh * blockSize) {
    throw new FormatError(`${data.length} bytes is too little for a ${width}×${height} block-compressed texture`);
  }
  const out = Buffer.alloc(width * height * 4);
  let offset = 0;
  for (let blockY = 0; blockY < blocksHigh; blockY++) {
    for (let blockX = 0; blockX < blocksWide; blockX++) {
      const alpha = hasAlphaBlock ? decodeBc3Alpha(data, offset) : null;
      const colorOffset = hasAlphaBlock ? offset + 8 : offset;
      const texels = decodeBc1Colors(data, colorOffset, hasAlphaBlock);
      offset += blockSize;
      for (let k = 0; k < 16; k++) {
        const x = blockX * BLOCK_TEXELS + (k & 3);
        const y = blockY * BLOCK_TEXELS + (k >> 2);
        // Blocks of a texture whose size is not a multiple of 4 extend past its edge.
        if (x >= width || y >= height) continue;
        const o = (y * width + x) * 4;
        const color = texels.colors[k];
        out[o] = color[0];
        out[o + 1] = color[1];
        out[o + 2] = color[2];
        out[o + 3] = alpha ? alpha[k] : texels.alphas[k];
      }
    }
  }
  return out;
}

// RGB565 → 8-bit channels, truncated (not rounded).
function expand565(value) {
  return [
    ((value >> 11) & 31) * 255 / 31 | 0,
    ((value >> 5) & 63) * 255 / 63 | 0,
    (value & 31) * 255 / 31 | 0,
  ];
}

// BC1 colour block: two RGB565 end points and 2-bit indices into a 4-entry palette. With c0 > c1, or always
// in BC3, the palette is c0, c1 and two interpolated colours; otherwise c0, c1, their average and transparent black.
function decodeBc1Colors(data, offset, alwaysFourColors) {
  const value0 = data.readUInt16LE(offset);
  const value1 = data.readUInt16LE(offset + 2);
  const c0 = expand565(value0);
  const c1 = expand565(value1);
  const fourColors = value0 > value1 || alwaysFourColors;
  const palette = fourColors
    ? [c0, c1, c0.map((x, i) => (2 * x + c1[i]) / 3 | 0), c0.map((x, i) => (x + 2 * c1[i]) / 3 | 0)]
    : [c0, c1, c0.map((x, i) => (x + c1[i]) / 2 | 0), [0, 0, 0]];
  const indices = data.readUInt32LE(offset + 4);
  const colors = [];
  const alphas = [];
  for (let k = 0; k < 16; k++) {
    const index = (indices >> (2 * k)) & 3;
    colors.push(palette[index]);
    alphas.push(!fourColors && index === 3 ? 0 : 255);
  }
  return {colors, alphas};
}

// BC3 alpha block: two 8-bit end points and 3-bit indices (48 bits, little-endian) into an 8-entry palette:
// with a0 > a1, a0, a1 and six interpolated values; otherwise a0, a1, four interpolated values, 0 and 255.
function decodeBc3Alpha(data, offset) {
  const a0 = data[offset];
  const a1 = data[offset + 1];
  const palette = [a0, a1];
  if (a0 > a1) {
    for (let k = 1; k < 7; k++) palette.push(((7 - k) * a0 + k * a1) / 7 | 0);
  } else {
    for (let k = 1; k < 5; k++) palette.push(((5 - k) * a0 + k * a1) / 5 | 0);
    palette.push(0, 255);
  }
  // Two 24-bit halves hold the indices of texels 0-7 and 8-15.
  const low = data[offset + 2] | (data[offset + 3] << 8) | (data[offset + 4] << 16);
  const high = data[offset + 5] | (data[offset + 6] << 8) | (data[offset + 7] << 16);
  const alpha = [];
  for (let k = 0; k < 16; k++) {
    const bits = k < 8 ? low >> (3 * k) : high >> (3 * (k - 8));
    alpha.push(palette[bits & 7]);
  }
  return alpha;
}

function bgraToRgba(data, width, height) {
  if (data.length < width * height * 4) {
    throw new FormatError(`${data.length} bytes is too little for ${width}×${height} BGRA`);
  }
  const out = Buffer.from(data.subarray(0, width * height * 4));
  for (let o = 0; o < out.length; o += 4) {
    out[o] = data[o + 2];
    out[o + 2] = data[o];
  }
  return out;
}

// Box filter with fractional source boxes, so sizes that do not divide evenly work too (the tapper icon is
// 266×266). Colours are averaged weighted by alpha, so transparent texels do not darken the edges.
export function resizeBox(pixels, width, height, targetWidth, targetHeight) {
  const out = Buffer.alloc(targetWidth * targetHeight * 4);
  const scaleX = width / targetWidth;
  const scaleY = height / targetHeight;
  for (let y = 0; y < targetHeight; y++) {
    for (let x = 0; x < targetWidth; x++) {
      let r = 0;
      let g = 0;
      let b = 0;
      let a = 0;
      let area = 0;
      const x0 = x * scaleX;
      const x1 = x0 + scaleX;
      const y0 = y * scaleY;
      const y1 = y0 + scaleY;
      for (let j = Math.floor(y0); j < Math.min(height, Math.ceil(y1)); j++) {
        const weightY = Math.min(y1, j + 1) - Math.max(y0, j);
        for (let i = Math.floor(x0); i < Math.min(width, Math.ceil(x1)); i++) {
          const weight = weightY * (Math.min(x1, i + 1) - Math.max(x0, i));
          const q = (j * width + i) * 4;
          const alphaWeight = pixels[q + 3] * weight;
          r += pixels[q] * alphaWeight;
          g += pixels[q + 1] * alphaWeight;
          b += pixels[q + 2] * alphaWeight;
          a += alphaWeight;
          area += weight;
        }
      }
      const o = (y * targetWidth + x) * 4;
      out[o] = a ? r / a : 0;
      out[o + 1] = a ? g / a : 0;
      out[o + 2] = a ? b / a : 0;
      out[o + 3] = area ? a / area : 0;
    }
  }
  return out;
}
