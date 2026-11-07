#include "pak.h"

#include <windows.h>

#include <algorithm>

#include "zlib.h"

namespace rd::extract {

namespace {

constexpr uint32_t kPakMagic = 0x5A6F12E1;
constexpr int32_t kPakVersion = 11;

// FPakInfo as serialized at the end of the file for version 11 with 5 compression method names.
constexpr int64_t kFooterSize = 221;
constexpr size_t kFooterEncryptedIndex = 16;   // u8 bEncryptedIndex, after the 16-byte EncryptionKeyGuid
constexpr int64_t kFooterMagic = 17;           // u32, then i32 version
constexpr int64_t kFooterIndexOffset = 25;     // i64 offset, i64 size, then the index SHA-1
constexpr size_t kFooterCompressionNames = 61; // 5 x 32-byte NUL-padded ANSI names
constexpr size_t kCompressionNameSize = 32;
constexpr size_t kCompressionNameCount = 5;

constexpr int64_t kSha1Size = 20;
// Offset (i64) + size (i64) + SHA-1 of a secondary index, stored when its "has index" flag is set.
constexpr int64_t kSecondaryIndexRefSize = 8 + 8 + kSha1Size;

// Serialized FPakEntry in front of every file's data: offset, size, uncompressed size (3 x i64),
// compression method (u32), SHA-1, flags (u8) and compression block size (u32). A compressed entry
// also stores its block table (i32 count + count x {i64 start, i64 end}) between the SHA-1 and the flags.
constexpr int64_t kEntryHeaderSize = 53;
constexpr int64_t kBlockTableCountSize = 4;
constexpr int64_t kBlockTableEntrySize = 16;

// Limits that keep a malformed pak from allocating without bound. ASTRONEER's index is a few MB and
// the files the extraction reads are at most about 1 MB.
constexpr int64_t kMaxIndexSize = 256ll << 20;
constexpr int64_t kMaxEntrySize = 64ll << 20;

// One zlib stream per compression block, inflated in place into the entry's buffer.
class Inflater {
public:
    Inflater() {
        if (inflateInit(&stream_) != Z_OK) throw std::runtime_error("zlib could not be initialised");
    }
    ~Inflater() { inflateEnd(&stream_); }
    Inflater(const Inflater&) = delete;
    Inflater& operator=(const Inflater&) = delete;

    // Returns the number of bytes written. The stream must end within the input; bytes after its end
    // are ignored.
    size_t Inflate(ByteView input, uint8_t* out, size_t capacity, const std::string& path) {
        inflateReset(&stream_);
        stream_.next_in = const_cast<Bytef*>(input.data());
        stream_.avail_in = (uInt)input.size();
        stream_.next_out = out;
        stream_.avail_out = (uInt)capacity;
        const int status = inflate(&stream_, Z_FINISH);
        if (status == Z_STREAM_END) return capacity - stream_.avail_out;
        if (status == Z_BUF_ERROR && stream_.avail_out == 0) {
            throw FormatError(path + " inflates to more bytes than its pak entry says");
        }
        if (status == Z_BUF_ERROR) throw std::runtime_error(path + ": unexpected end of the zlib data");
        throw std::runtime_error(path + ": " + (stream_.msg ? stream_.msg : "zlib error " + std::to_string(status)));
    }

private:
    z_stream stream_{};
};

}  // namespace

void Pak::HandleCloser::operator()(void* handle) const {
    CloseHandle(handle);
}

Pak::Pak(const std::filesystem::path& file) {
    const std::string name = PathToUtf8(file);
    // The game keeps its pak open; share everything so the extraction never blocks the game.
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) throw ToolError("ASTRONEER pak not found: " + name);
        throw ToolError("cannot open the pak " + name + " (Windows error " + std::to_string(error) + ")");
    }
    handle_.reset(handle);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size)) throw ToolError("cannot read the size of " + name);
    fileSize_ = size.QuadPart;

    const std::string notAPak = "not a v" + std::to_string(kPakVersion) + " pak: " + name;
    if (fileSize_ < kFooterSize) throw FormatError(notAPak);
    const Bytes footer = ReadAt(fileSize_ - kFooterSize, kFooterSize, kFooterSize);
    Reader footerReader(footer, kFooterMagic);
    const uint32_t magic = footerReader.U32();
    const int32_t version = footerReader.I32();
    if (magic != kPakMagic || version != kPakVersion) throw FormatError(notAPak);
    if (footer[kFooterEncryptedIndex] != 0) throw FormatError("encrypted pak index is not supported: " + name);
    footerReader.Seek(kFooterIndexOffset);
    const int64_t indexOffset = footerReader.I64();
    const int64_t indexSize = footerReader.I64();
    for (size_t k = 0; k < kCompressionNameCount; ++k) {
        const uint8_t* start = footer.data() + kFooterCompressionNames + k * kCompressionNameSize;
        const uint8_t* end = std::find(start, start + kCompressionNameSize, (uint8_t)0);
        compressionNames_.push_back(Latin1ToUtf8(ByteView(start, end)));
    }

    // The primary index: mount point, entry count, path hash seed, references to the path hash index
    // and the full directory index, then the encoded entries that directory locations point into.
    const Bytes index = ReadAt(indexOffset, indexSize, kMaxIndexSize);
    Reader reader(index);
    reader.FString();  // mount point
    reader.I32();      // entry count
    reader.U64();      // path hash seed
    if (reader.U32()) reader.Skip(kSecondaryIndexRefSize);  // path hash index
    if (!reader.U32()) throw FormatError("pak has no full directory index");
    const int64_t directoryOffset = reader.I64();
    const int64_t directorySize = reader.I64();
    reader.Skip(kSha1Size);
    const ByteView encoded = reader.Take(reader.I32());
    encodedEntries_.assign(encoded.begin(), encoded.end());

    // The full directory index: for each directory, its name and its files' names with their entry
    // locations. A path listed twice keeps its first place and its last location, like a JS Map.
    const Bytes directory = ReadAt(directoryOffset, directorySize, kMaxIndexSize);
    Reader directoryReader(directory);
    const uint32_t directoryCount = directoryReader.U32();
    for (uint32_t d = 0; d < directoryCount; ++d) {
        const std::string directoryName = directoryReader.FString();
        const uint32_t fileCount = directoryReader.U32();
        for (uint32_t f = 0; f < fileCount; ++f) {
            std::string path = directoryName + directoryReader.FString();
            const int32_t location = directoryReader.I32();
            if (!path.empty() && path[0] == '/') path.erase(0, 1);
            const auto [found, added] = fileIndex_.try_emplace(path, files_.size());
            if (added) {
                files_.push_back(std::move(path));
                locations_.push_back(location);
            } else {
                locations_[found->second] = location;
            }
        }
    }
}

Bytes Pak::ReadAt(int64_t offset, int64_t length, int64_t limit) const {
    if (offset < 0 || length < 0 || offset > fileSize_ || length > fileSize_ - offset) {
        throw FormatError("pak read of " + std::to_string(length) + " bytes at " + std::to_string(offset) +
                          " is outside the " + std::to_string(fileSize_) + "-byte file");
    }
    if (length > limit) {
        throw FormatError("pak read of " + std::to_string(length) + " bytes at " + std::to_string(offset) +
                          " is larger than this reader accepts (" + std::to_string(limit) + ")");
    }
    Bytes buffer((size_t)length);
    int64_t done = 0;
    while (done < length) {
        const uint64_t at = (uint64_t)(offset + done);
        OVERLAPPED overlapped{};
        overlapped.Offset = (DWORD)at;
        overlapped.OffsetHigh = (DWORD)(at >> 32);
        const DWORD wanted = (DWORD)std::min<int64_t>(length - done, 1 << 30);
        DWORD count = 0;
        if (!ReadFile(handle_.get(), buffer.data() + done, wanted, &count, &overlapped)) {
            throw std::runtime_error("cannot read the pak at " + std::to_string(at) + " (Windows error " +
                                     std::to_string(GetLastError()) + ")");
        }
        if (count == 0) throw FormatError("pak ended early at " + std::to_string(at));
        done += count;
    }
    return buffer;
}

// An encoded entry starts with a u32 bitfield:
//   bit 31      offset fits in 32 bits (else 64)
//   bit 30      uncompressed size fits in 32 bits
//   bit 29      compressed size fits in 32 bits
//   bits 28-23  compression method (0 = none, else 1-based index into the footer names)
//   bit 22      encrypted
//   bits 21-6   compression block count
//   bits 5-0    compression block size in 2 KB units; 0x3f means a u32 with the size follows
// then offset, uncompressed size, compressed size (only when compressed) and, unless there is exactly
// one unencrypted block (whose size is the compressed size), one u32 size per block.
Bytes Pak::Read(std::string_view pathView) const {
    const std::string path(pathView);
    const auto found = fileIndex_.find(path);
    if (found == fileIndex_.end()) throw FormatError("not in the pak: " + path);
    const int32_t location = locations_[found->second];
    if (location < 0) throw FormatError("non-encoded pak entry is not supported: " + path);

    Reader entry(encodedEntries_, location);
    const uint32_t bits = entry.U32();
    if ((bits & 0x3f) == 0x3f) entry.U32();  // block size, not needed to read the blocks
    const bool offsetIs32Bit = (bits >> 31) & 1;
    const bool uncompressedSizeIs32Bit = (bits >> 30) & 1;
    const bool sizeIs32Bit = (bits >> 29) & 1;
    const uint32_t compressionMethod = (bits >> 23) & 0x3f;
    const bool encrypted = (bits >> 22) & 1;
    const uint32_t blockCount = (bits >> 6) & 0xffff;

    auto varSize = [&entry](bool is32Bit) -> int64_t { return is32Bit ? entry.U32() : entry.U64(); };
    const int64_t offset = varSize(offsetIs32Bit);
    const int64_t uncompressedSize = varSize(uncompressedSizeIs32Bit);
    const int64_t size = compressionMethod ? varSize(sizeIs32Bit) : uncompressedSize;
    std::vector<int64_t> blockSizes;
    if (blockCount == 1 && !encrypted) {
        blockSizes.push_back(size);
    } else {
        for (uint32_t k = 0; k < blockCount; ++k) blockSizes.push_back(entry.U32());
    }

    if (encrypted) throw FormatError("encrypted pak entry is not supported: " + path);
    if (compressionMethod == 0) return ReadAt(offset + kEntryHeaderSize, uncompressedSize, kMaxEntrySize);
    const bool named = compressionMethod <= compressionNames_.size();
    if (!named || compressionNames_[compressionMethod - 1] != "Zlib") {
        const std::string method = named ? compressionNames_[compressionMethod - 1] : std::to_string(compressionMethod);
        throw FormatError("compression method " + method + " is not supported: " + path);
    }
    if (uncompressedSize > kMaxEntrySize) {
        throw FormatError(path + " is " + std::to_string(uncompressedSize) + " bytes, larger than this reader accepts");
    }

    int64_t position = offset + kEntryHeaderSize + kBlockTableCountSize + kBlockTableEntrySize * (int64_t)blockSizes.size();
    Bytes data((size_t)uncompressedSize + 1);  // the spare byte catches data that inflates to more
    size_t filled = 0;
    Inflater inflater;
    for (int64_t blockSize : blockSizes) {
        const Bytes block = ReadAt(position, blockSize, kMaxEntrySize);
        position += blockSize;
        filled += inflater.Inflate(block, data.data() + filled, data.size() - filled, path);
    }
    if (filled != (size_t)uncompressedSize) {
        throw FormatError(path + " inflated to " + std::to_string(filled) + " bytes instead of " +
                          std::to_string(uncompressedSize));
    }
    data.resize((size_t)uncompressedSize);
    return data;
}

}  // namespace rd::extract
