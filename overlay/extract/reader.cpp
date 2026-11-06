#include "reader.h"

#include <cstring>

namespace rd::extract {

namespace {

constexpr int64_t kMaxSafeInteger = (int64_t(1) << 53) - 1;  // Number.MAX_SAFE_INTEGER

int64_t SafeInteger(int64_t value) {
    if (value > kMaxSafeInteger || value < -kMaxSafeInteger) {
        throw FormatError("64-bit value " + std::to_string(value) + " is out of range");
    }
    return value;
}

template <class T>
T Load(const uint8_t* bytes) {
    T value;
    std::memcpy(&value, bytes, sizeof value);  // x64 is little-endian, like the files
    return value;
}

}  // namespace

void AppendUtf8(std::string& out, uint32_t codePoint) {
    if (codePoint < 0x80) {
        out += (char)codePoint;
    } else if (codePoint < 0x800) {
        out += (char)(0xC0 | (codePoint >> 6));
        out += (char)(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += (char)(0xE0 | (codePoint >> 12));
        out += (char)(0x80 | ((codePoint >> 6) & 0x3F));
        out += (char)(0x80 | (codePoint & 0x3F));
    } else {
        out += (char)(0xF0 | (codePoint >> 18));
        out += (char)(0x80 | ((codePoint >> 12) & 0x3F));
        out += (char)(0x80 | ((codePoint >> 6) & 0x3F));
        out += (char)(0x80 | (codePoint & 0x3F));
    }
}

std::string Latin1ToUtf8(ByteView bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (uint8_t byte : bytes) AppendUtf8(out, byte);
    return out;
}

std::string Utf16LeToUtf8(ByteView bytes) {
    std::string out;
    out.reserve(bytes.size());
    const size_t count = bytes.size() / 2;
    for (size_t i = 0; i < count; ++i) {
        uint32_t unit = bytes[2 * i] | (bytes[2 * i + 1] << 8);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < count) {
            const uint32_t next = bytes[2 * i + 2] | (bytes[2 * i + 3] << 8);
            if (next >= 0xDC00 && next <= 0xDFFF) {
                AppendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (next - 0xDC00));
                ++i;
                continue;
            }
        }
        if (unit >= 0xD800 && unit <= 0xDFFF) unit = 0xFFFD;
        AppendUtf8(out, unit);
    }
    return out;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

void Reader::Seek(int64_t position) {
    if (position < 0 || position > Size()) {
        throw FormatError("seek to " + std::to_string(position) + " outside a " + std::to_string(Size()) +
                          "-byte buffer");
    }
    position_ = position;
}

const uint8_t* Reader::Claim(int64_t count) {
    const int64_t start = position_;
    if (count < 0 || start < 0 || start > Size() || count > Size() - start) {
        throw FormatError("read of " + std::to_string(count) + " bytes at " + std::to_string(start) +
                          " past the end of a " + std::to_string(Size()) + "-byte buffer");
    }
    position_ = start + count;
    return data_.data() + start;
}

uint8_t Reader::U8() { return *Claim(1); }
int8_t Reader::I8() { return (int8_t)*Claim(1); }
int32_t Reader::I32() { return Load<int32_t>(Claim(4)); }
uint32_t Reader::U32() { return Load<uint32_t>(Claim(4)); }
float Reader::F32() { return Load<float>(Claim(4)); }
int64_t Reader::I64() { return SafeInteger(Load<int64_t>(Claim(8))); }

int64_t Reader::U64() {
    const uint64_t value = Load<uint64_t>(Claim(8));
    if (value > (uint64_t)kMaxSafeInteger) throw FormatError("64-bit value " + std::to_string(value) + " is out of range");
    return (int64_t)value;
}

ByteView Reader::Take(int64_t count) {
    const uint8_t* start = Claim(count);
    return ByteView(start, (size_t)count);
}

std::string Reader::FString() {
    const int64_t length = I32();
    if (length == 0) return {};
    const bool wide = length < 0;
    const int64_t unitSize = wide ? 2 : 1;
    const int64_t byteCount = (wide ? -length : length) * unitSize;
    const int64_t lengthAt = position_ - 4;
    const uint8_t* start = Claim(byteCount);
    const uint8_t* terminator = start + byteCount - unitSize;
    if (terminator[0] != 0 || (wide && terminator[1] != 0)) {
        throw FormatError("FString at " + std::to_string(lengthAt) + " has no terminating NUL");
    }
    const ByteView text(start, (size_t)(byteCount - unitSize));
    return wide ? Utf16LeToUtf8(text) : Latin1ToUtf8(text);
}

}  // namespace rd::extract
