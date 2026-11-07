#include "value.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace rd::extract {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

// The code point starting at text[i]; advances i. Invalid bytes decode as themselves (one byte).
uint32_t DecodeUtf8(std::string_view text, size_t& i) {
    const uint8_t lead = (uint8_t)text[i];
    const int length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (length == 1 || i + length > text.size()) {
        ++i;
        return lead;
    }
    uint32_t codePoint = lead & (0x7F >> length);
    for (int k = 1; k < length; ++k) codePoint = (codePoint << 6) | ((uint8_t)text[i + k] & 0x3F);
    i += length;
    return codePoint;
}

// WhiteSpace and LineTerminator of the ECMAScript grammar, which trim() removes.
bool IsJsSpace(uint32_t c) {
    switch (c) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
        return true;
    default:
        return c >= 0x2000 && c <= 0x200A;
    }
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Array.prototype.join(","), which String() of an array is.
std::string JoinArray(const Value& array) {
    std::string out;
    for (size_t k = 0; k < array.array.size(); ++k) {
        if (k) out += ',';
        const Value& element = array.array[k];
        if (element.type != Value::Type::Null) out += ToJsString(&element);
    }
    return out;
}

}  // namespace

Value Value::Null() { return Value(); }

Value Value::Bool(bool value) {
    Value v;
    v.type = Type::Bool;
    v.boolean = value;
    return v;
}

Value Value::Number(double value) {
    Value v;
    v.type = Type::Number;
    v.number = value;
    return v;
}

Value Value::String(std::string value) {
    Value v;
    v.type = Type::String;
    v.string = std::move(value);
    return v;
}

Value Value::EmptyArray() {
    Value v;
    v.type = Type::Array;
    return v;
}

Value Value::EmptyObject() {
    Value v;
    v.type = Type::Object;
    return v;
}

const Value* Value::Get(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const Member& member : object) {
        if (member.key == key) return &member.value;
    }
    return nullptr;
}

void Value::Set(std::string key, Value value) {
    for (Member& member : object) {
        if (member.key == key) {
            member.value = std::move(value);
            return;
        }
    }
    object.push_back({std::move(key), std::move(value)});
}

bool Truthy(const Value* value) {
    if (!value) return false;
    switch (value->type) {
    case Value::Type::Null: return false;
    case Value::Type::Bool: return value->boolean;
    case Value::Type::Number: return value->number != 0 && !std::isnan(value->number);
    case Value::Type::String: return !value->string.empty();
    default: return true;  // arrays and objects, even empty ones
    }
}

std::string ToJsString(const Value* value) {
    if (!value) return "undefined";
    switch (value->type) {
    case Value::Type::Null: return "null";
    case Value::Type::Bool: return value->boolean ? "true" : "false";
    case Value::Type::Number: return NumberToJsString(value->number);
    case Value::Type::String: return value->string;
    case Value::Type::Array: return JoinArray(*value);
    default: return "[object Object]";
    }
}

double ToJsNumber(const Value* value) {
    if (!value) return kNaN;
    switch (value->type) {
    case Value::Type::Null: return 0;
    case Value::Type::Bool: return value->boolean ? 1 : 0;
    case Value::Type::Number: return value->number;
    case Value::Type::String: return StringToJsNumber(value->string);
    case Value::Type::Array: return StringToJsNumber(JoinArray(*value));
    default: return kNaN;
    }
}

// The shortest digits that round-trip (as ECMAScript requires), laid out by its Number::toString rules.
std::string NumberToJsString(double value) {
    if (std::isnan(value)) return "NaN";
    if (value == 0) return "0";
    if (value < 0) return "-" + NumberToJsString(-value);
    if (std::isinf(value)) return "Infinity";

    char buffer[64];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    if (error != std::errc()) return "NaN";
    const std::string_view scientific(buffer, end - buffer);  // "d.ddde+XX" or "de+XX"
    const size_t e = scientific.find('e');
    std::string digits;
    for (char c : scientific.substr(0, e)) {
        if (c != '.') digits += c;
    }
    int exponent = 0;
    std::from_chars(scientific.data() + e + (scientific[e + 1] == '+' ? 2 : 1), scientific.data() + scientific.size(),
                    exponent);
    const int k = (int)digits.size();
    const int n = exponent + 1;  // value = 0.digits × 10^n

    if (k <= n && n <= 21) return digits + std::string(n - k, '0');
    if (0 < n && n <= 21) return digits.substr(0, n) + "." + digits.substr(n);
    if (-6 < n && n <= 0) return "0." + std::string(-n, '0') + digits;
    const std::string exponentText = (n - 1 >= 0 ? "e+" : "e-") + std::to_string(std::abs(n - 1));
    if (k == 1) return digits + exponentText;
    return digits.substr(0, 1) + "." + digits.substr(1) + exponentText;
}

// StringToNumber: decimal literals with an optional sign, Infinity, and 0x/0o/0b integers; blank is 0.
double StringToJsNumber(std::string_view text) {
    text = JsTrim(text);
    if (text.empty()) return 0;
    if (text.size() > 2 && text[0] == '0') {
        const char prefix = (char)(text[1] | 0x20);
        const int radix = prefix == 'x' ? 16 : prefix == 'o' ? 8 : prefix == 'b' ? 2 : 0;
        if (radix) {
            double result = 0;
            for (char c : text.substr(2)) {
                const char lower = (char)(c | 0x20);
                const int digit = IsDigit(c) ? c - '0' : (lower >= 'a' && lower <= 'f') ? lower - 'a' + 10 : 99;
                if (digit >= radix) return kNaN;
                result = result * radix + digit;
            }
            return result;
        }
    }
    const bool negative = text[0] == '-';
    std::string_view body = (text[0] == '+' || text[0] == '-') ? text.substr(1) : text;
    if (body == "Infinity") return negative ? -kInfinity : kInfinity;

    // digits [. digits] [e [sign] digits], at least one digit before the exponent
    size_t i = 0;
    size_t mantissaDigits = 0;
    while (i < body.size() && IsDigit(body[i])) ++i, ++mantissaDigits;
    if (i < body.size() && body[i] == '.') {
        ++i;
        while (i < body.size() && IsDigit(body[i])) ++i, ++mantissaDigits;
    }
    if (mantissaDigits == 0) return kNaN;
    if (i < body.size() && (body[i] == 'e' || body[i] == 'E')) {
        ++i;
        if (i < body.size() && (body[i] == '+' || body[i] == '-')) ++i;
        size_t exponentDigits = 0;
        while (i < body.size() && IsDigit(body[i])) ++i, ++exponentDigits;
        if (exponentDigits == 0) return kNaN;
    }
    if (i != body.size()) return kNaN;

    double result = 0;
    const auto [end, error] = std::from_chars(body.data(), body.data() + body.size(), result);
    if (error == std::errc::result_out_of_range) {
        // Too large or too small for a double: Infinity, or 0 for an underflow.
        const size_t exponentAt = body.find_first_of("eE");
        const bool overflow = exponentAt != std::string_view::npos
                                  ? body[exponentAt + 1] != '-'
                                  : body.find_first_of("123456789") < body.find('.');
        result = overflow ? kInfinity : 0;
    } else if (error != std::errc() || end != body.data() + body.size()) {
        return kNaN;
    }
    return negative ? -result : result;
}

double RoundFloat(float value) {
    const double x = value;
    if (!std::isfinite(x) || std::fabs(x) >= 1e21) return x;  // toFixed gives String(x) there
    // toFixed works on |x| and picks the integer n nearest to |x|·1000, the larger one on a tie.
    // |x|·1000 is exact: a float's 24-bit significand times 1000 fits in a double's 53 bits.
    const double scaled = std::fabs(x) * 1000;
    double n = std::floor(scaled);
    if (scaled - n >= 0.5) n += 1;
    const double rounded = n / 1000;  // the double nearest to the decimal "n/1000", as parsing it gives
    return x < 0 ? -rounded : rounded;
}

uint8_t ToUint8(double value) {
    if (!std::isfinite(value)) return 0;
    double wrapped = std::fmod(std::trunc(value), 256.0);
    if (wrapped < 0) wrapped += 256;
    return (uint8_t)wrapped;
}

std::string_view JsTrim(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size()) {
        size_t next = begin;
        if (!IsJsSpace(DecodeUtf8(text, next))) break;
        begin = next;
    }
    size_t end = text.size();
    while (end > begin) {
        size_t start = end - 1;
        while (start > begin && ((uint8_t)text[start] & 0xC0) == 0x80) --start;  // back to the lead byte
        size_t next = start;
        if (!IsJsSpace(DecodeUtf8(text, next)) || next != end) break;
        end = start;
    }
    return text.substr(begin, end - begin);
}

}  // namespace rd::extract
