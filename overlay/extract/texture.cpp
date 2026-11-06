#include "texture.h"

#include <algorithm>
#include <cmath>

#include "uasset.h"
#include "value.h"

namespace rd::extract {

namespace {

constexpr int64_t kStripDataFlagsSize = 2;           // FStripDataFlags: global and class strip flags
constexpr uint32_t kPackedDataHasOptData = 1u << 30;  // FTexturePlatformData::PackedData bit
constexpr int64_t kOptDataSize = 8;                   // FOptTexturePlatformData: ExtData, NumMipsInTail

constexpr uint32_t kBulkDataPayloadAtEndOfFile = 0x0001;
constexpr uint32_t kBulkDataSerializeCompressedZlib = 0x0002;
constexpr uint32_t kBulkDataUnused = 0x0020;
constexpr uint32_t kBulkDataPayloadInSeparateFile = 0x0100;
constexpr uint32_t kBulkDataSerializeCompressedBitWindow = 0x0200;
constexpr uint32_t kBulkDataOptionalPayload = 0x0800;
constexpr uint32_t kBulkDataSize64Bit = 0x2000;
constexpr uint32_t kBulkDataNotInline = kBulkDataPayloadAtEndOfFile | kBulkDataSerializeCompressedZlib |
                                        kBulkDataUnused | kBulkDataPayloadInSeparateFile |
                                        kBulkDataSerializeCompressedBitWindow | kBulkDataOptionalPayload;

// Icons are a few hundred texels wide. A larger size is refused, so a misread one cannot make the
// decoder allocate gigabytes.
constexpr int64_t kMaxTexels = 4096 * 4096;

constexpr int kBlockTexels = 4;
constexpr int64_t kBc1BlockSize = 8;
constexpr int64_t kBc3BlockSize = 16;

// A texture mip: its size, pixel format and raw bytes in that format.
struct Texture {
    int32_t width = 0;
    int32_t height = 0;
    std::string pixelFormat;
    Bytes data;
};

std::string Hex(uint32_t value) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    do {
        out.insert(out.begin(), kDigits[value & 15]);
        value >>= 4;
    } while (value);
    return out;
}

// The top mip of a cooked UE 4.27 Texture2D whose pixels are stored inline in the .uexp. Layout after
// the export's tagged properties: UTexture2D::Serialize, UTexture::SerializeCookedPlatformData,
// FTexturePlatformData::SerializeCooked, FTexture2DMipMap::Serialize, FUntypedBulkData::Serialize.
Texture ReadTexture2D(const Pak& pak, const std::string& objectPath) {
    const std::string packagePath = PackageOf(objectPath);
    const std::string file = PackageToPakPath(packagePath);
    const Bytes uexp = pak.Read(file + ".uexp");
    const Asset asset = ParseAsset(pak.Read(file + ".uasset"), uexp, packagePath);
    const auto found = std::find_if(asset.exports.begin(), asset.exports.end(),
                                    [&](const Export& e) { return e.path == objectPath; });
    if (found == asset.exports.end() || found->className != "Texture2D") throw FormatError("no Texture2D " + objectPath);
    if (!found->propertiesEnd) throw FormatError("unreadable texture properties in " + file);

    Reader reader(uexp, *found->propertiesEnd);
    SkipObjectGuid(reader);
    reader.Skip(kStripDataFlagsSize);  // UTexture
    reader.Skip(kStripDataFlagsSize);  // UTexture2D
    if (!reader.U32()) throw FormatError("texture is not cooked");
    const std::string pixelFormatName = ReadName(reader, asset.names);
    reader.I64();  // where the next pixel format's data starts

    // FTexturePlatformData
    reader.I32();  // SizeX of the full texture
    reader.I32();  // SizeY
    const uint32_t packedData = reader.U32();
    Texture texture;
    texture.pixelFormat = reader.FString();
    if (texture.pixelFormat != pixelFormatName) {
        throw FormatError("texture header misread (" + pixelFormatName + ", " + texture.pixelFormat + ")");
    }
    if (packedData & kPackedDataHasOptData) reader.Skip(kOptDataSize);
    reader.I32();  // FirstMipToSerialize
    if (reader.I32() < 1) throw FormatError("texture has no mips");

    // The first serialized mip: bCooked, then its bulk data header and payload, then its size.
    reader.U32();
    const uint32_t bulkFlags = reader.U32();
    if (bulkFlags & kBulkDataNotInline) {
        throw FormatError("texture mip is not stored inline (bulk data flags 0x" + Hex(bulkFlags) + ")");
    }
    const bool is64Bit = (bulkFlags & kBulkDataSize64Bit) != 0;
    const int64_t elementCount = is64Bit ? reader.I64() : reader.I32();
    const int64_t sizeOnDisk = is64Bit ? reader.I64() : reader.I32();
    reader.I64();  // OffsetInFile
    if (elementCount <= 0 || sizeOnDisk != elementCount) throw FormatError("texture mip has no plain inline data");
    const ByteView data = reader.Take(elementCount);
    texture.data.assign(data.begin(), data.end());
    texture.width = reader.I32();
    texture.height = reader.I32();
    return texture;
}

// RGB565 -> 8-bit channels, truncated (not rounded).
void Expand565(uint32_t value, int rgb[3]) {
    rgb[0] = (int)((value >> 11) & 31) * 255 / 31;
    rgb[1] = (int)((value >> 5) & 63) * 255 / 63;
    rgb[2] = (int)(value & 31) * 255 / 31;
}

// BC1 colour block: two RGB565 end points and 2-bit indices into a 4-entry palette. With c0 > c1, or
// always in BC3, the palette is c0, c1 and two interpolated colours; otherwise c0, c1, their average
// and transparent black.
void DecodeBc1Colors(const uint8_t* block, bool alwaysFourColors, uint8_t rgb[16][3], uint8_t alpha[16]) {
    const uint32_t value0 = block[0] | (block[1] << 8);
    const uint32_t value1 = block[2] | (block[3] << 8);
    int palette[4][3];
    Expand565(value0, palette[0]);
    Expand565(value1, palette[1]);
    const bool fourColors = value0 > value1 || alwaysFourColors;
    for (int i = 0; i < 3; ++i) {
        if (fourColors) {
            palette[2][i] = (2 * palette[0][i] + palette[1][i]) / 3;
            palette[3][i] = (palette[0][i] + 2 * palette[1][i]) / 3;
        } else {
            palette[2][i] = (palette[0][i] + palette[1][i]) / 2;
            palette[3][i] = 0;
        }
    }
    const uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | ((uint32_t)block[7] << 24);
    for (int k = 0; k < 16; ++k) {
        const uint32_t index = (indices >> (2 * k)) & 3;
        for (int i = 0; i < 3; ++i) rgb[k][i] = (uint8_t)palette[index][i];
        alpha[k] = !fourColors && index == 3 ? 0 : 255;
    }
}

// BC3 alpha block: two 8-bit end points and 3-bit indices (48 bits, little-endian) into an 8-entry
// palette: with a0 > a1, a0, a1 and six interpolated values; otherwise a0, a1, four interpolated
// values, 0 and 255.
void DecodeBc3Alpha(const uint8_t* block, uint8_t alpha[16]) {
    const int a0 = block[0];
    const int a1 = block[1];
    int palette[8] = {a0, a1};
    if (a0 > a1) {
        for (int k = 1; k < 7; ++k) palette[k + 1] = ((7 - k) * a0 + k * a1) / 7;
    } else {
        for (int k = 1; k < 5; ++k) palette[k + 1] = ((5 - k) * a0 + k * a1) / 5;
        palette[6] = 0;
        palette[7] = 255;
    }
    // Two 24-bit halves hold the indices of texels 0-7 and 8-15.
    const uint32_t low = block[2] | (block[3] << 8) | (block[4] << 16);
    const uint32_t high = block[5] | (block[6] << 8) | (block[7] << 16);
    for (int k = 0; k < 16; ++k) {
        const uint32_t bits = k < 8 ? low >> (3 * k) : high >> (3 * (k - 8));
        alpha[k] = (uint8_t)palette[bits & 7];
    }
}

// DXT1 and DXT5 are BC1 and BC3 block compression: 4x4 texel blocks, 8 bytes of colour (BC1) preceded
// in BC3 by 8 bytes of alpha.
Bytes DecodeBlocks(const Texture& texture, bool hasAlphaBlock) {
    const int64_t width = texture.width;
    const int64_t height = texture.height;
    const int64_t blockSize = hasAlphaBlock ? kBc3BlockSize : kBc1BlockSize;
    const int64_t blocksWide = (width + kBlockTexels - 1) / kBlockTexels;
    const int64_t blocksHigh = (height + kBlockTexels - 1) / kBlockTexels;
    if ((int64_t)texture.data.size() < blocksWide * blocksHigh * blockSize) {
        throw FormatError(std::to_string(texture.data.size()) + " bytes is too little for a " + std::to_string(width) +
                          "×" + std::to_string(height) + " block-compressed texture");
    }
    Bytes out((size_t)(width * height * 4));
    const uint8_t* block = texture.data.data();
    for (int64_t blockY = 0; blockY < blocksHigh; ++blockY) {
        for (int64_t blockX = 0; blockX < blocksWide; ++blockX, block += blockSize) {
            uint8_t blockAlpha[16];
            if (hasAlphaBlock) DecodeBc3Alpha(block, blockAlpha);
            uint8_t rgb[16][3];
            uint8_t colorAlpha[16];
            DecodeBc1Colors(hasAlphaBlock ? block + 8 : block, hasAlphaBlock, rgb, colorAlpha);
            for (int k = 0; k < 16; ++k) {
                const int64_t x = blockX * kBlockTexels + (k & 3);
                const int64_t y = blockY * kBlockTexels + (k >> 2);
                // Blocks of a texture whose size is not a multiple of 4 extend past its edge.
                if (x >= width || y >= height) continue;
                uint8_t* texel = out.data() + (y * width + x) * 4;
                texel[0] = rgb[k][0];
                texel[1] = rgb[k][1];
                texel[2] = rgb[k][2];
                texel[3] = hasAlphaBlock ? blockAlpha[k] : colorAlpha[k];
            }
        }
    }
    return out;
}

Bytes BgraToRgba(const Texture& texture) {
    const size_t size = (size_t)texture.width * texture.height * 4;
    if (texture.data.size() < size) {
        throw FormatError(std::to_string(texture.data.size()) + " bytes is too little for " +
                          std::to_string(texture.width) + "×" + std::to_string(texture.height) + " BGRA");
    }
    Bytes out(texture.data.begin(), texture.data.begin() + size);
    for (size_t o = 0; o < size; o += 4) std::swap(out[o], out[o + 2]);
    return out;
}

// RGBA8 pixels, row by row, for the pixel formats ASTRONEER's icons use.
Bytes DecodeToRgba(const Texture& texture) {
    if (texture.width < 0 || texture.height < 0 || (int64_t)texture.width * texture.height > kMaxTexels) {
        throw FormatError("texture size " + std::to_string(texture.width) + "×" + std::to_string(texture.height) +
                          " is not an icon's");
    }
    if (texture.pixelFormat == "PF_DXT1") return DecodeBlocks(texture, false);
    if (texture.pixelFormat == "PF_DXT5") return DecodeBlocks(texture, true);
    if (texture.pixelFormat == "PF_B8G8R8A8") return BgraToRgba(texture);
    throw FormatError("unsupported pixel format " + texture.pixelFormat);
}

// Box filter with fractional source boxes, so sizes that do not divide evenly work too (the tapper
// icon is 266x266). Colours are averaged weighted by alpha, so transparent texels do not darken the
// edges. The arithmetic is dxt.mjs's, in doubles and in the same order, so the bytes match.
Bytes ResizeBox(const Bytes& pixels, int width, int height, int targetWidth, int targetHeight) {
    Bytes out((size_t)targetWidth * targetHeight * 4);
    const double scaleX = (double)width / targetWidth;
    const double scaleY = (double)height / targetHeight;
    for (int y = 0; y < targetHeight; ++y) {
        for (int x = 0; x < targetWidth; ++x) {
            double r = 0;
            double g = 0;
            double b = 0;
            double a = 0;
            double area = 0;
            const double x0 = x * scaleX;
            const double x1 = x0 + scaleX;
            const double y0 = y * scaleY;
            const double y1 = y0 + scaleY;
            const double rowEnd = std::min((double)height, std::ceil(y1));
            const double columnEnd = std::min((double)width, std::ceil(x1));
            for (int j = (int)std::floor(y0); j < rowEnd; ++j) {
                const double weightY = std::min(y1, (double)j + 1) - std::max(y0, (double)j);
                for (int i = (int)std::floor(x0); i < columnEnd; ++i) {
                    const double weight = weightY * (std::min(x1, (double)i + 1) - std::max(x0, (double)i));
                    const size_t q = ((size_t)j * width + i) * 4;
                    const double alphaWeight = pixels[q + 3] * weight;
                    r += pixels[q] * alphaWeight;
                    g += pixels[q + 1] * alphaWeight;
                    b += pixels[q + 2] * alphaWeight;
                    a += alphaWeight;
                    area += weight;
                }
            }
            uint8_t* texel = out.data() + ((size_t)y * targetWidth + x) * 4;
            texel[0] = ToUint8(a != 0 ? r / a : 0);
            texel[1] = ToUint8(a != 0 ? g / a : 0);
            texel[2] = ToUint8(a != 0 ? b / a : 0);
            texel[3] = ToUint8(area != 0 ? a / area : 0);
        }
    }
    return out;
}

}  // namespace

Bytes ReadIcon(const Pak& pak, const std::string& objectPath) {
    const Texture texture = ReadTexture2D(pak, objectPath);
    Bytes pixels = DecodeToRgba(texture);
    if (texture.width == kIconSize && texture.height == kIconSize) return pixels;
    return ResizeBox(pixels, texture.width, texture.height, kIconSize, kIconSize);
}

}  // namespace rd::extract
