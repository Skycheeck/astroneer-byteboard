#pragma once
// Little-endian cursor over a byte buffer, shared by every binary reader in extract/, and the errors
// they throw. Reads the UE4 primitive encodings: integers, floats, raw bytes and FString (int32 length,
// then Latin-1 or, for a negative length, UTF-16LE characters, both with a terminating NUL that the
// length counts). Every read is bounds-checked and throws FormatError instead of reading past the end.
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace rd::extract {

// The input does not have the binary layout the reader expects (wrong file, other engine version).
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A problem that stops the extraction with a message for the user (the pak is missing, the game data
// contradicts itself).
class ToolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using Bytes = std::vector<uint8_t>;
using ByteView = std::span<const uint8_t>;

// Text is UTF-8 throughout. Lone UTF-16 surrogates become U+FFFD.
std::string Latin1ToUtf8(ByteView bytes);
std::string Utf16LeToUtf8(ByteView bytes);
void AppendUtf8(std::string& out, uint32_t codePoint);
// For messages; path::string() would use the ANSI code page.
std::string PathToUtf8(const std::filesystem::path& path);

class Reader {
public:
    // position may lie outside the buffer; the first read then fails.
    explicit Reader(ByteView data, int64_t position = 0) : data_(data), position_(position) {}

    int64_t Position() const { return position_; }
    int64_t Size() const { return (int64_t)data_.size(); }
    void Seek(int64_t position);
    void Skip(int64_t count) { Seek(position_ + count); }

    uint8_t U8();
    int8_t I8();
    int32_t I32();
    uint32_t U32();
    float F32();
    // 64-bit values are offsets and sizes here; anything beyond 2^53 means the layout was misread.
    int64_t I64();
    int64_t U64();
    ByteView Take(int64_t count);
    std::string FString();

private:
    const uint8_t* Claim(int64_t count);

    ByteView data_;
    int64_t position_;
};

}  // namespace rd::extract
