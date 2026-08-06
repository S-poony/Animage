// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace animage {

// Just enough JSON to write and read a scene file.
//
// Written here rather than taken from somewhere because `animage_core` has no
// external dependencies and this is not the place to acquire the first one --
// the same reason the max-flow and the half-float are ours. It is deliberately
// small: no comments, no trailing commas, no duplicate keys. It has to read
// what it writes, and refuse a corrupt file loudly rather than guess.
//
// Objects are written with their keys sorted. JSON objects carry no order, so
// nothing that reads a scene depends on the order the writer set its keys;
// sorting is what makes the file the same bytes twice running -- the plan
// chose JSON so that a project is "readable, diffable, debuggable by hand",
// and a hash would reorder the file on every save and make every diff useless.
// Sorted is the strongest form of that guarantee: the same document is the
// same bytes however its keys were set. Whatever order matters lives in the
// arrays.
class Json {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Json() = default;

    // Named rather than constructors: `Json(0)` would otherwise be a bool on
    // some days and a number on others, and the compiler would not say which.
    static Json boolean(bool value);
    static Json number(double value);
    static Json text(std::string value);
    static Json array();
    static Json object();

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool isArray() const { return kind_ == Kind::Array; }
    bool isObject() const { return kind_ == Kind::Object; }

    // Every reader takes a fallback and none of them throw. A field that is
    // missing or of the wrong type reads as the fallback, so a file from an
    // older version loads with the defaults rather than failing.
    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    float asFloat(float fallback = 0.0f) const;
    int asInt(int fallback = 0) const;
    std::uint64_t asId(std::uint64_t fallback = 0) const;
    std::string asText(std::string fallback = {}) const;

    // Objects. `has` distinguishes an absent key from one holding null.
    void set(std::string key, Json value);
    bool has(std::string_view key) const;
    const Json& operator[](std::string_view key) const;

    // Arrays.
    void push(Json value);
    std::size_t size() const;
    const Json& at(std::size_t index) const;

    // Two spaces of indent per level, one line per value. Pass 0 for compact.
    std::string dump(int indent = 2) const;

    // False on malformed input, with `error` describing what and where. `out`
    // is left untouched on failure.
    static bool parse(std::string_view source, Json& out, std::string* error = nullptr);

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string text_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

}  // namespace animage
