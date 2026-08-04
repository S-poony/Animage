// SPDX-License-Identifier: GPL-3.0-or-later
#include "json.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace animage {
namespace {

const Json& nullValue() {
    static const Json nothing;
    return nothing;
}

void writeEscaped(const std::string& text, std::string& out) {
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                // Anything else goes through as bytes, which keeps UTF-8 in a
                // layer name readable in the file rather than turned into
                // escapes nobody can read.
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0xf];
                    out += kHex[c & 0xf];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void writeNumber(double value, std::string& out) {
    if (!std::isfinite(value)) {  // no JSON spelling for these; 0 is honest
        out += '0';
        return;
    }
    char buffer[32];
    // Shortest form that reads back identically, so the file stays legible.
    //
    // Tried as a float first, because almost every number here is one: opacity,
    // colour channels, ids. Widening 0.6f to double and asking for the shortest
    // double gives 0.6000000238418579, which is exact, useless to read, and
    // makes a diff of two saves unreadable. If the float round-trips to the same
    // double it is the same number, written the way it was meant.
    const float narrowed = static_cast<float>(value);
    const auto result = (static_cast<double>(narrowed) == value)
                            ? std::to_chars(buffer, buffer + sizeof(buffer), narrowed)
                            : std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

// The leaves. Arrays and objects are written by dump() itself, which is the only
// thing that can see their storage.
void writeScalar(const Json& value, std::string& out) {
    switch (value.kind()) {
        case Json::Kind::Bool: out += value.asBool() ? "true" : "false"; break;
        case Json::Kind::Number: writeNumber(value.asNumber(), out); break;
        case Json::Kind::String: writeEscaped(value.asText(), out); break;
        default: out += "null"; break;
    }
}

}  // namespace

Json Json::boolean(bool value) {
    Json json;
    json.kind_ = Kind::Bool;
    json.bool_ = value;
    return json;
}

Json Json::number(double value) {
    Json json;
    json.kind_ = Kind::Number;
    json.number_ = value;
    return json;
}

Json Json::text(std::string value) {
    Json json;
    json.kind_ = Kind::String;
    json.text_ = std::move(value);
    return json;
}

Json Json::array() {
    Json json;
    json.kind_ = Kind::Array;
    return json;
}

Json Json::object() {
    Json json;
    json.kind_ = Kind::Object;
    return json;
}

bool Json::asBool(bool fallback) const { return kind_ == Kind::Bool ? bool_ : fallback; }

double Json::asNumber(double fallback) const {
    return kind_ == Kind::Number ? number_ : fallback;
}

float Json::asFloat(float fallback) const {
    return kind_ == Kind::Number ? static_cast<float>(number_) : fallback;
}

int Json::asInt(int fallback) const {
    return kind_ == Kind::Number ? static_cast<int>(number_) : fallback;
}

std::uint64_t Json::asId(std::uint64_t fallback) const {
    if (kind_ != Kind::Number || number_ < 0.0) return fallback;
    return static_cast<std::uint64_t>(number_);
}

std::string Json::asText(std::string fallback) const {
    return kind_ == Kind::String ? text_ : std::move(fallback);
}

void Json::set(std::string key, Json value) {
    kind_ = Kind::Object;
    for (auto& [name, held] : object_) {
        if (name == key) {
            held = std::move(value);
            return;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
}

bool Json::has(std::string_view key) const {
    for (const auto& [name, held] : object_) {
        if (name == key) return true;
    }
    return false;
}

const Json& Json::operator[](std::string_view key) const {
    for (const auto& [name, held] : object_) {
        if (name == key) return held;
    }
    return nullValue();
}

void Json::push(Json value) {
    kind_ = Kind::Array;
    array_.push_back(std::move(value));
}

std::size_t Json::size() const {
    if (kind_ == Kind::Array) return array_.size();
    if (kind_ == Kind::Object) return object_.size();
    return 0;
}

const Json& Json::at(std::size_t index) const {
    if (kind_ != Kind::Array || index >= array_.size()) return nullValue();
    return array_[index];
}

std::string Json::dump(int indent) const {
    std::string out;
    const auto write = [&](const Json& value, int depth, auto&& self) -> void {
        const auto newline = [&](int level) {
            if (indent <= 0) return;
            out += '\n';
            out.append(static_cast<std::size_t>(indent * level), ' ');
        };

        if (value.kind_ == Kind::Array) {
            if (value.array_.empty()) { out += "[]"; return; }
            out += '[';
            for (std::size_t i = 0; i < value.array_.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                self(value.array_[i], depth + 1, self);
            }
            newline(depth);
            out += ']';
            return;
        }
        if (value.kind_ != Kind::Object) {
            writeScalar(value, out);
            return;
        }

        if (value.object_.empty()) { out += "{}"; return; }
        out += '{';
        for (std::size_t i = 0; i < value.object_.size(); ++i) {
            if (i) out += ',';
            newline(depth + 1);
            writeEscaped(value.object_[i].first, out);
            out += indent > 0 ? ": " : ":";
            self(value.object_[i].second, depth + 1, self);
        }
        newline(depth);
        out += '}';
    };

    write(*this, 0, write);
    return out;
}

namespace {

// A recursive-descent parser over the whole string. `at` is the read position
// and every failure reports it, because "unexpected character" without an
// offset is no use on a file with ten thousand lines in it.
class Parser {
public:
    Parser(std::string_view source, std::string* error) : source_(source), error_(error) {}

    bool parseValue(Json& out) {
        skipSpace();
        if (at_ >= source_.size()) return fail("unexpected end of input");

        switch (source_[at_]) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                std::string text;
                if (!parseString(text)) return false;
                out = Json::text(std::move(text));
                return true;
            }
            case 't': return parseLiteral("true", Json::boolean(true), out);
            case 'f': return parseLiteral("false", Json::boolean(false), out);
            case 'n': return parseLiteral("null", Json(), out);
            default: return parseNumber(out);
        }
    }

    bool atEnd() {
        skipSpace();
        return at_ >= source_.size();
    }

    bool fail(const std::string& what) {
        if (error_ && error_->empty()) {
            *error_ = what + " at byte " + std::to_string(at_);
        }
        return false;
    }

private:
    void skipSpace() {
        while (at_ < source_.size()) {
            const char c = source_[at_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++at_;
        }
    }

    bool parseLiteral(std::string_view word, Json value, Json& out) {
        if (source_.substr(at_, word.size()) != word) return fail("bad literal");
        at_ += word.size();
        out = std::move(value);
        return true;
    }

    bool parseNumber(Json& out) {
        const std::size_t start = at_;
        if (at_ < source_.size() && (source_[at_] == '-' || source_[at_] == '+')) ++at_;
        while (at_ < source_.size()) {
            const char c = source_[at_];
            const bool part = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                              ((c == '-' || c == '+') && (source_[at_ - 1] == 'e' ||
                                                          source_[at_ - 1] == 'E'));
            if (!part) break;
            ++at_;
        }
        if (at_ == start) return fail("expected a value");

        double value = 0.0;
        const char* begin = source_.data() + start;
        const auto result = std::from_chars(begin, source_.data() + at_, value);
        if (result.ec != std::errc{} || result.ptr != source_.data() + at_) {
            at_ = start;
            return fail("malformed number");
        }
        out = Json::number(value);
        return true;
    }

    bool parseString(std::string& out) {
        if (at_ >= source_.size() || source_[at_] != '"') return fail("expected a string");
        ++at_;
        out.clear();
        while (true) {
            if (at_ >= source_.size()) return fail("unterminated string");
            const char c = source_[at_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= source_.size()) return fail("unterminated escape");
            switch (source_[at_++]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (at_ + 4 > source_.size()) return fail("short \\u escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = source_[at_ + static_cast<std::size_t>(i)];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("bad \\u escape");
                    }
                    at_ += 4;
                    // We only ever emit \u for control characters, so one byte
                    // out is enough for anything we wrote. A surrogate pair from
                    // some other writer would come back mangled; the format is
                    // ours and does not contain one.
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xc0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    } else {
                        out += static_cast<char>(0xe0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    }
                    break;
                }
                default: return fail("unknown escape");
            }
        }
    }

    bool parseArray(Json& out) {
        ++at_;  // '['
        Json result = Json::array();
        skipSpace();
        if (at_ < source_.size() && source_[at_] == ']') {
            ++at_;
            out = std::move(result);
            return true;
        }
        while (true) {
            Json element;
            if (!parseValue(element)) return false;
            result.push(std::move(element));
            skipSpace();
            if (at_ >= source_.size()) return fail("unterminated array");
            if (source_[at_] == ',') { ++at_; continue; }
            if (source_[at_] == ']') { ++at_; break; }
            return fail("expected ',' or ']'");
        }
        out = std::move(result);
        return true;
    }

    bool parseObject(Json& out) {
        ++at_;  // '{'
        Json result = Json::object();
        skipSpace();
        if (at_ < source_.size() && source_[at_] == '}') {
            ++at_;
            out = std::move(result);
            return true;
        }
        while (true) {
            skipSpace();
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (at_ >= source_.size() || source_[at_] != ':') return fail("expected ':'");
            ++at_;
            Json value;
            if (!parseValue(value)) return false;
            result.set(std::move(key), std::move(value));
            skipSpace();
            if (at_ >= source_.size()) return fail("unterminated object");
            if (source_[at_] == ',') { ++at_; continue; }
            if (source_[at_] == '}') { ++at_; break; }
            return fail("expected ',' or '}'");
        }
        out = std::move(result);
        return true;
    }

    std::string_view source_;
    std::string* error_;
    std::size_t at_ = 0;
};

}  // namespace

bool Json::parse(std::string_view source, Json& out, std::string* error) {
    if (error) error->clear();
    Parser parser(source, error);
    Json parsed;
    if (!parser.parseValue(parsed)) return false;
    if (!parser.atEnd()) return parser.fail("trailing characters");
    out = std::move(parsed);
    return true;
}

}  // namespace animage
