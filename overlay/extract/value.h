#pragma once
// Property values as tools/lib/uasset.mjs produces them for JavaScript, and the JavaScript semantics
// the catalog rules apply to them (truthiness, String(), Number(), Object.keys order, string order),
// so the port makes the same decisions as the Node pipeline it replaces.
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

    // The member named key, or nullptr (JavaScript's undefined); only objects have members.
    const Value* Get(std::string_view key) const;
    // Adds or replaces a member; a replaced member keeps its place, as in a JavaScript object.
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
// +value.toFixed(3), how uasset.mjs rounds a FloatProperty.
double RoundFloat(float value);
// The integer part modulo 256, as storing a number into a Buffer does.
uint8_t ToUint8(double value);

// String.prototype.trim() on UTF-8 text.
std::string_view JsTrim(std::string_view text);
// a < b as JavaScript compares strings: by UTF-16 code units.
bool JsStringLess(std::string_view a, std::string_view b);
// The order Object.keys lists keys inserted in this order: keys that are array indices ("0" to
// "4294967294") first, ascending, then the others in insertion order. Returns positions into keys.
std::vector<size_t> JsKeyOrder(const std::vector<std::string>& keys);

}  // namespace rd::extract
