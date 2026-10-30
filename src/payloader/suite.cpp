// PayLoader - The Signal suite: ps5-exporter, ps5-homeassistant and ps5-signal.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/suite.hpp"

#include "payloader/repo_status.hpp"

#include <cctype>

namespace hui::payloader
{

namespace
{

std::string_view trim_url(std::string_view url)
{
    while (!url.empty() &&
           (std::isspace(static_cast<unsigned char>(url.back())) || url.back() == '/'))
        url.remove_suffix(1);
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front())))
        url.remove_prefix(1);
    return url;
}

// "repo/ps5-exporter@1a2b3c4d" -> "ps5-exporter"; "ps5-exporter.elf" -> "ps5-exporter".
std::string_view bare_name(std::string_view entry)
{
    const std::size_t slash = entry.rfind('/');
    if (slash != std::string_view::npos)
        entry.remove_prefix(slash + 1);
    const std::size_t at = entry.find('@');
    if (at != std::string_view::npos)
        entry = entry.substr(0, at);
    if (entry.size() > 4 && entry.substr(entry.size() - 4) == ".elf")
        entry.remove_suffix(4);
    return entry;
}

} // namespace

bool is_suite_url(std::string_view url)
{
    return trim_url(url) == kSuiteUrl;
}

const RepoSource *suite_source(const std::vector<RepoSource> &sources)
{
    for (const RepoSource &source : sources)
        if (is_suite_url(source.url))
            return &source;
    return nullptr;
}

SuiteProgress suite_progress(const RepoSource &suite, const PayloadList &installed)
{
    SuiteProgress progress;
    for (const RepoPayload &payload : suite.payloads)
    {
        if (suite_rank(payload.key()) < 0)
            continue;
        const RepoStatus status = repo_status(payload, suite.url, installed);
        progress.installed += status.kind == RepoStatus::Kind::installed ? 1 : 0;
        progress.update = progress.update || status.kind == RepoStatus::Kind::update;
    }
    return progress;
}

int suite_rank(std::string_view entry)
{
    const std::string_view name = bare_name(entry);
    for (std::size_t i = 0; i < kSuiteIds.size(); ++i)
        if (name == kSuiteIds[i])
            return static_cast<int>(i);
    return -1;
}

std::vector<AutoloadEntry> with_suite_entry(std::vector<AutoloadEntry> list,
                                            const std::string &target)
{
    if (autoload_contains(list, target))
        return list;
    const int rank = suite_rank(target);
    if (rank >= 0)
    {
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            // It and a wait go where that payload is, so the wait before that payload stays in
            // front of it.
            if (!list[i].wait && suite_rank(list[i].target) > rank)
            {
                list.insert(list.begin() + static_cast<std::ptrdiff_t>(i),
                            {AutoloadEntry::payload(target), AutoloadEntry::pause(kSuiteWaitMs)});
                return list;
            }
        }
    }
    if (!list.empty() && !list.back().wait)
        list.push_back(AutoloadEntry::pause(kSuiteWaitMs));
    list.push_back(AutoloadEntry::payload(target));
    return list;
}

} // namespace hui::payloader
