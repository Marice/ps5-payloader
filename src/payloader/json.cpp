// PayLoader - A small JSON reader and writer for the pldmgr API.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/json.hpp"

#include <cstdio>
#include <cstdlib>

namespace hui::payloader
{

namespace
{

constexpr int kMaxDepth = 32;

const JsonValue &null_value()
{
    static const JsonValue kNull;
    return kNull;
}

} // namespace

const JsonValue &JsonValue::operator[](std::string_view key) const
{
    for (const auto &member : members_)
    {
        if (member.first == key)
            return member.second;
    }
    return null_value();
}

bool JsonValue::has(std::string_view key) const
{
    for (const auto &member : members_)
    {
        if (member.first == key)
            return true;
    }
    return false;
}

class JsonParser
{
  public:
    explicit JsonParser(std::string_view text) : text_(text)
    {
    }

    bool document(JsonValue &out)
    {
        skip();
        if (!value(out, 0))
            return false;
        skip();
        return at_ == text_.size();
    }

  private:
    void skip()
    {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(at_, word.size()) != word)
            return false;
        at_ += word.size();
        return true;
    }

    bool value(JsonValue &out, int depth)
    {
        if (depth > kMaxDepth || at_ >= text_.size())
            return false;
        const char c = text_[at_];
        if (c == '{')
            return object(out, depth);
        if (c == '[')
            return array(out, depth);
        if (c == '"')
        {
            out.type_ = JsonValue::Type::string;
            return string(out.text_);
        }
        if (c == 't' || c == 'f')
        {
            out.type_ = JsonValue::Type::boolean;
            out.boolean_ = c == 't';
            return literal(c == 't' ? "true" : "false");
        }
        if (c == 'n')
        {
            out.type_ = JsonValue::Type::null;
            return literal("null");
        }
        return number(out);
    }

    bool object(JsonValue &out, int depth)
    {
        out.type_ = JsonValue::Type::object;
        ++at_;
        skip();
        if (at_ < text_.size() && text_[at_] == '}')
        {
            ++at_;
            return true;
        }
        while (true)
        {
            skip();
            std::string key;
            if (at_ >= text_.size() || text_[at_] != '"' || !string(key))
                return false;
            skip();
            if (at_ >= text_.size() || text_[at_] != ':')
                return false;
            ++at_;
            skip();
            JsonValue member;
            if (!value(member, depth + 1))
                return false;
            out.members_.emplace_back(std::move(key), std::move(member));
            skip();
            if (at_ >= text_.size())
                return false;
            if (text_[at_] == '}')
            {
                ++at_;
                return true;
            }
            if (text_[at_] != ',')
                return false;
            ++at_;
        }
    }

    bool array(JsonValue &out, int depth)
    {
        out.type_ = JsonValue::Type::array;
        ++at_;
        skip();
        if (at_ < text_.size() && text_[at_] == ']')
        {
            ++at_;
            return true;
        }
        while (true)
        {
            skip();
            JsonValue item;
            if (!value(item, depth + 1))
                return false;
            out.items_.push_back(std::move(item));
            skip();
            if (at_ >= text_.size())
                return false;
            if (text_[at_] == ']')
            {
                ++at_;
                return true;
            }
            if (text_[at_] != ',')
                return false;
            ++at_;
        }
    }

    bool hex4(unsigned &code)
    {
        if (at_ + 4 > text_.size())
            return false;
        code = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char h = text_[at_++];
            code <<= 4;
            if (h >= '0' && h <= '9')
                code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f')
                code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                code |= static_cast<unsigned>(h - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    static void append_utf8(std::string &out, unsigned code)
    {
        if (code < 0x80)
        {
            out += static_cast<char>(code);
        }
        else if (code < 0x800)
        {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else if (code < 0x10000)
        {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool string(std::string &out)
    {
        ++at_; // the opening quote
        while (at_ < text_.size())
        {
            const char c = text_[at_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (at_ >= text_.size())
                return false;
            const char e = text_[at_++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                unsigned code = 0;
                if (!hex4(code))
                    return false;
                // A surrogate pair carries one code point above U+FFFF.
                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    unsigned low = 0;
                    if (at_ + 2 > text_.size() || text_[at_] != '\\' || text_[at_ + 1] != 'u')
                        return false;
                    at_ += 2;
                    if (!hex4(low) || low < 0xDC00 || low > 0xDFFF)
                        return false;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                else if (code >= 0xDC00 && code <= 0xDFFF)
                {
                    return false;
                }
                append_utf8(out, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool number(JsonValue &out)
    {
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-')
            ++at_;
        bool digits = false;
        while (at_ < text_.size() &&
               ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.' ||
                text_[at_] == 'e' || text_[at_] == 'E' || text_[at_] == '+' || text_[at_] == '-'))
        {
            digits = digits || (text_[at_] >= '0' && text_[at_] <= '9');
            ++at_;
        }
        if (!digits || at_ - start > 40)
            return false;
        char buffer[48];
        const std::size_t length = at_ - start;
        text_.copy(buffer, length, start);
        buffer[length] = '\0';
        char *end = nullptr;
        out.number_ = std::strtod(buffer, &end);
        if (end != buffer + length)
            return false;
        out.type_ = JsonValue::Type::number;
        return true;
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

bool parse_json(std::string_view text, JsonValue &out)
{
    JsonValue value;
    JsonParser parser(text);
    if (!parser.document(value))
        return false;
    out = std::move(value);
    return true;
}

void append_json_string(std::string &out, std::string_view text)
{
    out += '"';
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (static_cast<unsigned char>(c) < 0x20)
        {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
            out += escape;
        }
        else
        {
            out += c;
        }
    }
    out += '"';
}

} // namespace hui::payloader
