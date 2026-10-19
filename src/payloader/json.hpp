// PayLoader - A small JSON reader and writer for the pldmgr API.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr answers in JSON objects and takes some back. This
// reads a document into a tree of values and writes strings safely; it is
// not a general-purpose library (numbers as double; \u escapes become
// UTF-8).

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui::payloader
{

class JsonValue
{
  public:
    enum class Type : unsigned char
    {
        null,
        boolean,
        number,
        string,
        array,
        object,
    };

    Type type() const
    {
        return type_;
    }
    bool is_null() const
    {
        return type_ == Type::null;
    }
    bool is_object() const
    {
        return type_ == Type::object;
    }
    bool is_array() const
    {
        return type_ == Type::array;
    }
    bool is_number() const
    {
        return type_ == Type::number;
    }
    bool is_bool() const
    {
        return type_ == Type::boolean;
    }
    bool is_string() const
    {
        return type_ == Type::string;
    }

    bool as_bool(bool fallback = false) const
    {
        return type_ == Type::boolean ? boolean_ : fallback;
    }
    double as_number(double fallback = 0.0) const
    {
        return type_ == Type::number ? number_ : fallback;
    }
    const std::string &as_string() const
    {
        return text_;
    }

    // An object's member, or a null value when there is none.
    const JsonValue &operator[](std::string_view key) const;
    bool has(std::string_view key) const;
    const std::vector<std::pair<std::string, JsonValue>> &members() const
    {
        return members_;
    }
    const std::vector<JsonValue> &items() const
    {
        return items_;
    }

  private:
    friend class JsonParser;

    Type type_ = Type::null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string text_;
    std::vector<JsonValue> items_;
    std::vector<std::pair<std::string, JsonValue>> members_;
};

// Parses a whole document. Returns false (and leaves out untouched) when the
// text is not valid JSON or nests deeper than 32 levels.
bool parse_json(std::string_view text, JsonValue &out);

// Appends text as a JSON string, quotes included.
void append_json_string(std::string &out, std::string_view text);

} // namespace hui::payloader
