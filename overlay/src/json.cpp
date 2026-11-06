#include "json.h"

#include <charconv>

namespace rd::json {

namespace {

// Deeper documents are not ours; the limit keeps a broken file from exhausting the stack.
constexpr int kMaxDepth = 64;

const Value& NullValue() {
    static const Value value;
    return value;
}

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

}  // namespace

bool Value::AsBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Value::AsNumber(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

const std::string& Value::AsString() const {
    static const std::string empty;
    return type_ == Type::String ? string_ : empty;
}

const std::vector<Value>& Value::Items() const {
    static const std::vector<Value> empty;
    return type_ == Type::Array ? items_ : empty;
}

const Value& Value::operator[](std::string_view key) const {
    if (type_ != Type::Object) return NullValue();
    for (const Member& m : members_) {
        if (m.key == key) return m.value;
    }
    return NullValue();
}

// Recursive descent over the text; every Parse* function starts at the first character of its
// production and leaves pos_ just after it.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool ParseDocument(Value& out) {
        SkipWhitespace();
        if (!ParseValue(out, 0)) return false;
        SkipWhitespace();
        if (pos_ != text_.size()) return Fail("unexpected text after the value");
        return true;
    }

    const std::string& Error() const { return error_; }

private:
    bool Fail(const char* what) {
        if (error_.empty()) error_ = std::string(what) + " at offset " + std::to_string(pos_);
        return false;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) ++pos_;
    }

    bool Consume(char c) {
        if (pos_ >= text_.size() || text_[pos_] != c) return false;
        ++pos_;
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return Fail("nesting too deep");
        if (pos_ >= text_.size()) return Fail("unexpected end");
        switch (text_[pos_]) {
            case '{':
                return ParseObject(out, depth);
            case '[':
                return ParseArray(out, depth);
            case '"':
                out.type_ = Type::String;
                return ParseString(out.string_);
            case 't':
                out.type_ = Type::Bool;
                out.bool_ = true;
                return ParseLiteral("true");
            case 'f':
                out.type_ = Type::Bool;
                out.bool_ = false;
                return ParseLiteral("false");
            case 'n':
                out.type_ = Type::Null;
                return ParseLiteral("null");
            default:
                return ParseNumber(out);
        }
    }

    bool ParseLiteral(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) return Fail("unknown literal");
        pos_ += word.size();
        return true;
    }

    bool ParseNumber(Value& out) {
        const size_t start = pos_;
        while (pos_ < text_.size() && std::string_view("+-.0123456789eE").find(text_[pos_]) != std::string_view::npos) ++pos_;
        if (pos_ == start) return Fail("unexpected character");
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        double value = 0;
        const auto [end, ec] = std::from_chars(first, last, value);
        if (ec != std::errc() || end != last) return Fail("bad number");
        out.type_ = Type::Number;
        out.number_ = value;
        return true;
    }

    bool ParseHex4(unsigned& out) {
        if (pos_ + 4 > text_.size()) return Fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }

    bool ParseEscape(std::string& out) {
        if (pos_ >= text_.size()) return Fail("unexpected end in a string");
        const char c = text_[pos_++];
        switch (c) {
            case '"':
            case '\\':
            case '/':
                out += c;
                return true;
            case 'b':
                out += '\b';
                return true;
            case 'f':
                out += '\f';
                return true;
            case 'n':
                out += '\n';
                return true;
            case 'r':
                out += '\r';
                return true;
            case 't':
                out += '\t';
                return true;
            case 'u':
                break;
            default:
                return Fail("bad escape");
        }
        unsigned cp = 0;
        if (!ParseHex4(cp)) return false;
        // A high surrogate followed by a low one is one code point; an unpaired one becomes U+FFFD.
        if (cp >= 0xD800 && cp <= 0xDBFF && text_.substr(pos_, 2) == "\\u") {
            const size_t save = pos_;
            pos_ += 2;
            unsigned low = 0;
            if (!ParseHex4(low)) return false;
            if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            else pos_ = save;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        AppendUtf8(out, cp);
        return true;
    }

    bool ParseString(std::string& out) {
        if (!Consume('"')) return Fail("expected a string");
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (!ParseEscape(out)) return false;
        }
        return Fail("unterminated string");
    }

    bool ParseArray(Value& out, int depth) {
        Consume('[');
        out.type_ = Type::Array;
        SkipWhitespace();
        if (Consume(']')) return true;
        for (;;) {
            SkipWhitespace();
            out.items_.emplace_back();
            if (!ParseValue(out.items_.back(), depth + 1)) return false;
            SkipWhitespace();
            if (Consume(']')) return true;
            if (!Consume(',')) return Fail("expected ',' or ']'");
        }
    }

    bool ParseObject(Value& out, int depth) {
        Consume('{');
        out.type_ = Type::Object;
        SkipWhitespace();
        if (Consume('}')) return true;
        for (;;) {
            SkipWhitespace();
            Value::Member member;
            if (!ParseString(member.key)) return false;
            SkipWhitespace();
            if (!Consume(':')) return Fail("expected ':'");
            SkipWhitespace();
            if (!ParseValue(member.value, depth + 1)) return false;
            out.members_.push_back(std::move(member));
            SkipWhitespace();
            if (Consume('}')) return true;
            if (!Consume(',')) return Fail("expected ',' or '}'");
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
    std::string error_;
};

std::optional<Value> Parse(std::string_view text, std::string* error) {
    Parser parser(text);
    Value value;
    if (parser.ParseDocument(value)) return value;
    if (error) *error = parser.Error();
    return std::nullopt;
}

}  // namespace rd::json
