#pragma once
// Property values (encoding in uasset.h) and the conversions the catalog rules apply to them. The rules
// take JavaScript's semantics, which the extractor was first written in and which fit loosely typed
// data well: a missing property is undefined (nullptr), and truthiness, String() and Number() decide
// what a value of an unexpected type means.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rd::extract {

struct Member;

struct Value {
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<Member> object;  // insertion order, unique keys

    static Value Null();
    static Value Bool(bool value);
    static Value Number(double value);
    static Value String(std::string value);
    static Value EmptyArray();
    static Value EmptyObject();

    // The member named key, or nullptr (undefined); only objects have members.
    const Value* Get(std::string_view key) const;
    // Adds or replaces a member; a replaced member keeps its place.
    void Set(std::string key, Value value);
};

struct Member {
    std::string key;
    Value value;
};

// nullptr stands for undefined in all of these.
bool Truthy(const Value* value);
std::string ToJsString(const Value* value);  // String(value)
double ToJsNumber(const Value* value);       // Number(value)

std::string NumberToJsString(double value);  // Number.prototype.toString()
double StringToJsNumber(std::string_view text);
// +value.toFixed(3): how a FloatProperty is rounded (to 3 decimals, half away from zero).
double RoundFloat(float value);
// The integer part modulo 256 (0 for NaN and infinities): how a computed channel becomes a byte.
uint8_t ToUint8(double value);

// String.prototype.trim() on UTF-8 text: Unicode white space and line breaks at both ends.
std::string_view JsTrim(std::string_view text);

}  // namespace rd::extract
