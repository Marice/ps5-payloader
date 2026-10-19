// PayLoader - pldmgr's autoload list: payloads to start, in order, with waits between them.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr hands the list over as one comma-separated text: a payload slot
// ("local/ftpsrv", "repo/kstuff@1a2b3c4d"), a path, or "!<ms>" for a wait.
// It keeps it in a 4096-byte buffer, one line per entry.
//
//   std::vector<AutoloadEntry> list = parse_autoload_list("local/a,!2000,repo/b@h");
//   list.insert(list.begin() + 1, AutoloadEntry::payload("local/c"));
//   std::string text;
//   if (format_autoload_list(list, text)) save(text);

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hui::payloader
{

constexpr int kMaxWaitMs = 3600000;
constexpr std::size_t kMaxAutoloadBytes =
    4094; // one line per entry, plus the last line break, in 4096

struct AutoloadEntry
{
    bool wait = false;
    int wait_ms = 0;    // for a wait
    std::string target; // for a payload: a slot or a path

    static AutoloadEntry payload(std::string target);
    static AutoloadEntry pause(int ms);
    bool operator==(const AutoloadEntry &) const = default;
};

// Skips empty and malformed parts (a wait out of range, a bare "!").
std::vector<AutoloadEntry> parse_autoload_list(std::string_view text);

// False when a target is empty or holds a comma or a control character, a wait is
// out of range, or the text would be longer than pldmgr keeps.
bool format_autoload_list(const std::vector<AutoloadEntry> &list, std::string &out);

// Moves entry index one place up (-1) or down (+1). False at either end.
bool move_entry(std::vector<AutoloadEntry> &list, std::size_t index, int step);

// Whether the list names this slot or path.
bool autoload_contains(const std::vector<AutoloadEntry> &list, std::string_view target);

} // namespace hui::payloader
