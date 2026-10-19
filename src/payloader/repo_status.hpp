// PayLoader - Whether a repository payload is installed, newer, or installed from elsewhere.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rules of pldmgr's web UI (frontend/src/utils/payloadStatus.js, v0.6.1):
// a repository payload belongs to an installed one with the same id from the
// same repository (the official repository's old and new address count as
// one). It is an update when its version is newer, or the same with another
// checksum. The same id from another repository is a conflict: installing
// asks whether to replace that one. pldmgr itself is compared with the
// version that runs (or the one downloaded for its next start).
//
//   const RepoStatus status = repo_status(payload, source.url, list);
//   if (status.kind == RepoStatus::Kind::update) offer_update(*status.installed);

#pragma once

#include "payloader/pldmgr.hpp"

#include <string_view>

namespace hui::payloader
{

struct RepoStatus
{
    enum class Kind : unsigned char
    {
        not_installed,
        installed, // the same or an older version
        update,
    };
    Kind kind = Kind::not_installed;
    const Payload *installed = nullptr; // from this repository
    const Payload *conflict = nullptr;  // the same id from another repository
    bool self = false;                  // this is pldmgr itself
};

bool is_official_repository(std::string_view url);

RepoStatus repo_status(const RepoPayload &payload, std::string_view source_url,
                       const PayloadList &installed);

// Whether PayLoader offers it: pldmgr shows itself only from the official repository.
bool repo_payload_visible(const RepoPayload &payload, std::string_view source_url);

} // namespace hui::payloader
