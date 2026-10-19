// PayLoader - pldmgr's autoload list: payloads to start, in order, with waits between them.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/autoload_list.hpp"

#include <utility>

namespace hui::payloader
{

namespace
{

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

bool parse_wait(std::string_view digits, int &ms)
{
    if (digits.empty() || digits.size() > 7)
        return false;
    int value = 0;
    for (const char c : digits)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    if (value > kMaxWaitMs)
        return false;
    ms = value;
    return true;
}

} // namespace

AutoloadEntry AutoloadEntry::payload(std::string target)
{
    AutoloadEntry entry;
    entry.target = std::move(target);
    return entry;
}

AutoloadEntry AutoloadEntry::pause(int ms)
{
    AutoloadEntry entry;
    entry.wait = true;
    entry.wait_ms = ms;
    return entry;
}

std::vector<AutoloadEntry> parse_autoload_list(std::string_view text)
{
    std::vector<AutoloadEntry> list;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t comma = text.find(',', start);
        if (comma == std::string_view::npos)
            comma = text.size();
        const std::string_view part = trim(text.substr(start, comma - start));
        start = comma + 1;
        if (part.empty())
            continue;
        if (part.front() == '!')
        {
            int ms = 0;
            if (parse_wait(part.substr(1), ms))
                list.push_back(AutoloadEntry::pause(ms));
            continue;
        }
        list.push_back(AutoloadEntry::payload(std::string(part)));
    }
    return list;
}

bool format_autoload_list(const std::vector<AutoloadEntry> &list, std::string &out)
{
    std::string text;
    for (const AutoloadEntry &entry : list)
    {
        if (!text.empty())
            text += ',';
        if (entry.wait)
        {
            if (entry.wait_ms < 0 || entry.wait_ms > kMaxWaitMs)
                return false;
            text += '!';
            text += std::to_string(entry.wait_ms);
            continue;
        }
        const std::string_view target = trim(entry.target);
        if (target.empty() || target.front() == '!' || target.find(',') != std::string_view::npos)
            return false;
        for (const char c : target)
            if (static_cast<unsigned char>(c) < 32 || c == 127)
                return false;
        text += target;
    }
    // pldmgr stores one entry per line: the commas become line breaks, same length.
    if (text.size() > kMaxAutoloadBytes)
        return false;
    out = std::move(text);
    return true;
}

bool move_entry(std::vector<AutoloadEntry> &list, std::size_t index, int step)
{
    if (index >= list.size() || (step != -1 && step != 1))
        return false;
    if (step < 0 ? index == 0 : index + 1 >= list.size())
        return false;
    const std::size_t other = step < 0 ? index - 1 : index + 1;
    std::swap(list[index], list[other]);
    return true;
}

bool autoload_contains(const std::vector<AutoloadEntry> &list, std::string_view target)
{
    for (const AutoloadEntry &entry : list)
        if (!entry.wait && entry.target == target)
            return true;
    return false;
}

} // namespace hui::payloader
