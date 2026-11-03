// A small JSON reader for the data the website's pages carry (numbers, strings, booleans, arrays and objects). It is
// platform-neutral and reads a whole text at once; a malformed text is reported, never guessed at.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace esrwiki::json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Value> items;     // the elements of an array
    std::vector<std::string> keys;  // the keys of an object, in order, with the matching values in values
    std::vector<Value> values;

    // The value of an object member, or a null value when it is missing.
    const Value& Field(const std::string& key) const {
        static const Value none;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return values[i];
        }
        return none;
    }
};

namespace detail {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool Parse(Value& out, std::string& error) {
        SkipSpace();
        if (!ParseValue(out, error)) return false;
        SkipSpace();
        if (pos_ != text_.size()) return Fail(error, "text after the value");
        return true;
    }

private:
    bool Fail(std::string& error, const std::string& what) {
        error = what + " at byte " + std::to_string(pos_);
        return false;
    }

    void SkipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool Literal(const char* word) {
        const std::string w = word;
        if (text_.compare(pos_, w.size(), w) != 0) return false;
        pos_ += w.size();
        return true;
    }

    bool ParseValue(Value& out, std::string& error) {
        if (pos_ >= text_.size()) return Fail(error, "unexpected end");
        const char c = text_[pos_];
        if (c == '{') return ParseObject(out, error);
        if (c == '[') return ParseArray(out, error);
        if (c == '"') {
            out.kind = Value::Kind::String;
            return ParseString(out.text, error);
        }
        if (Literal("true")) {
            out.kind = Value::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (Literal("false")) {
            out.kind = Value::Kind::Bool;
            out.boolean = false;
            return true;
        }
        if (Literal("null")) {
            out.kind = Value::Kind::Null;
            return true;
        }
        return ParseNumber(out, error);
    }

    bool ParseNumber(Value& out, std::string& error) {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '-' ||
                                       text_[pos_] == '+' || text_[pos_] == '.' || text_[pos_] == 'e' ||
                                       text_[pos_] == 'E')) {
            ++pos_;
        }
        if (pos_ == start) return Fail(error, "unexpected character");
        char* end = nullptr;
        const std::string token = text_.substr(start, pos_ - start);
        out.number = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') return Fail(error, "bad number");
        out.kind = Value::Kind::Number;
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned long code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool ParseHex4(unsigned long& code, std::string& error) {
        if (pos_ + 4 > text_.size()) return Fail(error, "bad escape");
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned long>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned long>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned long>(h - 'A' + 10);
            else return Fail(error, "bad escape");
        }
        return true;
    }

    bool ParseString(std::string& out, std::string& error) {
        ++pos_;  // the opening quote
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) break;
            const char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned long code = 0;
                    if (!ParseHex4(code, error)) return false;
                    if (code >= 0xD800 && code < 0xDC00 && pos_ + 6 <= text_.size() && text_[pos_] == '\\' &&
                        text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned long low = 0;
                        if (!ParseHex4(low, error)) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default: return Fail(error, "bad escape");
            }
        }
        return Fail(error, "unterminated string");
    }

    bool ParseArray(Value& out, std::string& error) {
        out.kind = Value::Kind::Array;
        ++pos_;
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipSpace();
            out.items.emplace_back();
            if (!ParseValue(out.items.back(), error)) return false;
            SkipSpace();
            if (pos_ >= text_.size()) return Fail(error, "unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return Fail(error, "bad array");
        }
    }

    bool ParseObject(Value& out, std::string& error) {
        out.kind = Value::Kind::Object;
        ++pos_;
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return Fail(error, "object key expected");
            std::string key;
            if (!ParseString(key, error)) return false;
            SkipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return Fail(error, "':' expected");
            ++pos_;
            SkipSpace();
            out.keys.push_back(key);
            out.values.emplace_back();
            if (!ParseValue(out.values.back(), error)) return false;
            SkipSpace();
            if (pos_ >= text_.size()) return Fail(error, "unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return Fail(error, "bad object");
        }
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

}  // namespace detail

// Reads one JSON value from the whole text. On failure `error` says where, and `out` is not meant to be used.
inline bool Parse(const std::string& text, Value& out, std::string& error) {
    detail::Parser parser(text);
    return parser.Parse(out, error);
}

}  // namespace esrwiki::json
