#pragma once
// A small JSON reader for the files the Lua mod writes (state.json and results.json, see
// docs/PROTOCOL.md). It parses RFC 8259 JSON into a tree; unlike the RFC it also accepts raw
// control characters inside strings. Strings stay UTF-8, \u escapes are decoded to UTF-8.
// Threads: any; there is no shared state.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rd::json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    struct Member;

    Type GetType() const { return type_; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsArray() const { return type_ == Type::Array; }

    // Each accessor returns the fallback (or an empty value) when the value has another type.
    bool AsBool(bool fallback) const;
    double AsNumber(double fallback) const;
    const std::string& AsString() const;
    const std::vector<Value>& Items() const;
    // The member with this key, or a null value when it is missing or this is not an object.
    const Value& operator[](std::string_view key) const;

private:
    friend class Parser;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Value> items_;
    std::vector<Member> members_;
};

struct Value::Member {
    std::string key;
    Value value;
};

// The parsed document, or nullopt with a short description in *error.
std::optional<Value> Parse(std::string_view text, std::string* error = nullptr);

}  // namespace rd::json
