// PayLoader - Whether a repository payload is installed, newer, or installed from elsewhere.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/repo_status.hpp"

#include "payloader/versions.hpp"

#include <cctype>
#include <string>

namespace hui::payloader
{

namespace
{

constexpr std::string_view kOfficial = "https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json";
constexpr std::string_view kOfficialLegacy =
    "https://itsplk.github.io/ps5-payloads-mirror/payloads.json";

std::string_view normalize(std::string_view url)
{
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front())))
        url.remove_prefix(1);
    while (!url.empty() && (std::isspace(static_cast<unsigned char>(url.back())) || url.back() == '/'))
        url.remove_suffix(1);
    return url;
}

bool same_repository(std::string_view a, std::string_view b)
{
    return normalize(a) == normalize(b) || (is_official_repository(a) && is_official_repository(b));
}

bool same_checksum(std::string_view a, std::string_view b)
{
    a = normalize(a);
    b = normalize(b);
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool is_self(const RepoPayload &payload, std::string_view source_url)
{
    return payload.key() == "pldmgr" && is_official_repository(source_url);
}

} // namespace

bool is_official_repository(std::string_view url)
{
    url = normalize(url);
    return url == kOfficial || url == kOfficialLegacy;
}

bool repo_payload_visible(const RepoPayload &payload, std::string_view source_url)
{
    if (is_self(payload, source_url))
        return true;
    return !is_system_payload(payload.id) && !is_system_payload(payload.name) &&
           !is_system_payload(payload.filename);
}

RepoStatus repo_status(const RepoPayload &payload, std::string_view source_url,
                       const PayloadList &installed)
{
    RepoStatus status;
    if (is_self(payload, source_url))
    {
        status.self = true;
        const std::string &running = installed.running_version;
        const std::string &pending = installed.pending_version;
        const std::string &have =
            !pending.empty() && compare_versions(pending, running) > 0 ? pending : running;
        status.kind = !have.empty() && compare_versions(payload.version, have) > 0
                          ? RepoStatus::Kind::update
                          : RepoStatus::Kind::installed;
        return status;
    }

    const std::string id = payload.key();
    for (const Payload &item : installed.items)
    {
        if (item.kind != "repo" || item.id != id)
            continue;
        if (same_repository(item.source_url, source_url))
        {
            if (status.installed == nullptr)
                status.installed = &item;
        }
        else if (status.conflict == nullptr)
        {
            status.conflict = &item;
        }
    }
    if (status.installed == nullptr)
        return status;

    status.conflict = nullptr;
    const Payload &have = *status.installed;
    const int order =
        compare_versions(payload.version, have.version, payload.last_update, have.last_update);
    const bool other_build = order == 0 && !payload.checksum.empty() && !have.checksum.empty() &&
                             !same_checksum(payload.checksum, have.checksum);
    status.kind = order > 0 || other_build ? RepoStatus::Kind::update : RepoStatus::Kind::installed;
    return status;
}

} // namespace hui::payloader
