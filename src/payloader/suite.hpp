// PayLoader - The Signal suite: ps5-exporter, ps5-homeassistant and ps5-signal.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three payloads work together: ps5-homeassistant reads ps5-exporter and uses ps5-signal for
// beeps and the light. They come from github.com/Marice/ps5-signal-suite, a repository in pldmgr's
// format that PayLoader keeps in pldmgr's sources. In a boot list they go in suite order, a few
// seconds apart, the rule Signal Companion uses too (its signal/autoload.cpp, autoload_with).
//
//   if (const RepoSource *suite = suite_source(view.repository)) offer(*suite);
//   list = with_suite_entry(list, "ps5-signal.elf");

#pragma once

#include "payloader/autoload_list.hpp"
#include "payloader/pldmgr.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hui::payloader
{

constexpr const char *kSuiteUrl =
    "https://raw.githubusercontent.com/Marice/ps5-signal-suite/main/payloads.json";
constexpr const char *kSuiteName = "Signal suite";
constexpr int kSuiteWaitMs = 3000;

// The suite's payload ids, in the order they start.
constexpr std::array<const char *, 3> kSuiteIds = {"ps5-exporter", "ps5-homeassistant",
                                                   "ps5-signal"};

bool is_suite_url(std::string_view url);
// The suite's source in pldmgr's repository list, or null.
const RepoSource *suite_source(const std::vector<RepoSource> &sources);

// The suite position of a boot list entry, a payload id or file name: 0, 1, 2, or -1 for another.
// "ps5-exporter", "ps5-exporter.elf", "repo/ps5-exporter@1a2b3c4d" and "local/ps5-exporter" all
// count.
int suite_rank(std::string_view entry);

// The list with target placed for its suite rank: before the first suite payload that comes after
// it, with a wait, else last after a wait. Unchanged when target is listed already.
std::vector<AutoloadEntry> with_suite_entry(std::vector<AutoloadEntry> list,
                                            const std::string &target);

} // namespace hui::payloader
