// PayLoader - Version order, slugs and pldmgr's own names, as pldmgr's web UI has them.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/versions.hpp"

#include <cctype>
#include <vector>

namespace hui::payloader
{

namespace
{

struct Token
{
    bool digits;
    std::string_view text;
};

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

std::vector<Token> tokens(std::string_view version)
{
    std::string_view s = trim(version);
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V'))
        s = trim(s.substr(1));
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size())
    {
        const std::size_t start = i;
        if (is_digit(s[i]))
        {
            while (i < s.size() && is_digit(s[i]))
                ++i;
            out.push_back({true, s.substr(start, i - start)});
        }
        else if (is_letter(s[i]))
        {
            while (i < s.size() && is_letter(s[i]))
                ++i;
            out.push_back({false, s.substr(start, i - start)});
        }
        else
        {
            ++i;
        }
    }
    return out;
}

int compare_numbers(std::string_view a, std::string_view b)
{
    while (a.size() > 1 && a.front() == '0')
        a.remove_prefix(1);
    while (b.size() > 1 && b.front() == '0')
        b.remove_prefix(1);
    if (a.size() != b.size())
        return a.size() > b.size() ? 1 : -1;
    return a == b ? 0 : (a > b ? 1 : -1);
}

int compare_letters(std::string_view a, std::string_view b)
{
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const int x = std::tolower(static_cast<unsigned char>(a[i]));
        const int y = std::tolower(static_cast<unsigned char>(b[i]));
        if (x != y)
            return x > y ? 1 : -1;
    }
    return a.size() == b.size() ? 0 : (a.size() > b.size() ? 1 : -1);
}

// The web UI parses dates where it can and compares text otherwise. pldmgr
// writes ISO dates, where text order is time order.
int tie_break(std::string_view a, std::string_view b)
{
    if (a.empty() || b.empty() || a == b)
        return 0;
    return a > b ? 1 : -1;
}

} // namespace

int compare_versions(std::string_view a, std::string_view b, std::string_view a_date,
                     std::string_view b_date)
{
    if (a.empty() && b.empty())
        return tie_break(a_date, b_date);
    if (a.empty())
        return -1;
    if (b.empty())
        return 1;
    const std::vector<Token> ta = tokens(a);
    const std::vector<Token> tb = tokens(b);
    if (ta.empty() && tb.empty())
        return tie_break(a_date, b_date);
    if (ta.empty())
        return -1;
    if (tb.empty())
        return 1;
    const std::size_t n = ta.size() > tb.size() ? ta.size() : tb.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i >= ta.size())
            return tb[i].digits ? -1 : 1; // b goes on: a sub-version is newer, a pre-release older
        if (i >= tb.size())
            return ta[i].digits ? 1 : -1;
        if (ta[i].digits != tb[i].digits)
            return ta[i].digits ? 1 : -1;
        const int c = ta[i].digits ? compare_numbers(ta[i].text, tb[i].text)
                                   : compare_letters(ta[i].text, tb[i].text);
        if (c != 0)
            return c;
    }
    return tie_break(a_date, b_date);
}

std::string slug(std::string_view name)
{
    std::string s;
    bool last_dash = true;
    for (const char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80 && std::isalnum(u))
        {
            s += static_cast<char>(std::tolower(u));
            last_dash = false;
        }
        else if ((c == '.' || c == '_') && !s.empty() && s.back() != '.')
        {
            s += c;
            last_dash = false;
        }
        else if (!last_dash)
        {
            s += '-';
            last_dash = true;
        }
    }
    while (!s.empty() && (s.back() == '-' || s.back() == '.' || s.back() == '_'))
        s.pop_back();
    return s.empty() ? "payload" : s;
}

bool is_system_payload(std::string_view name)
{
    const std::size_t slash = name.rfind('/');
    if (slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    std::string lower;
    for (const char c : name)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.empty())
        return false;
    return lower.rfind("pldmgr", 0) == 0 || lower.find("payload-manager") != std::string::npos ||
           lower.find("payload_manager") != std::string::npos ||
           lower.find("payload manager") != std::string::npos ||
           lower.find("payloadmanager") != std::string::npos;
}

bool version_at_least(std::string_view version, int major, int minor, int patch)
{
    // A version is one word: "0.6.1", "v1.0-beta"; "404 Not Found" is not one.
    for (const char c : trim(version))
        if (std::isspace(static_cast<unsigned char>(c)))
            return false;
    const std::vector<Token> t = tokens(version);
    if (t.empty() || !t[0].digits)
        return false;
    const int wanted[3] = {major, minor, patch};
    for (std::size_t i = 0; i < 3; ++i)
    {
        int have = 0;
        if (i < t.size() && t[i].digits)
        {
            if (t[i].text.size() > 6)
                return true;
            have = std::stoi(std::string(t[i].text));
        }
        else if (i < t.size())
        {
            return false; // "0.6beta": a pre-release of 0.6.0 is not 0.6.0
        }
        if (have != wanted[i])
            return have > wanted[i];
    }
    return true;
}

} // namespace hui::payloader
