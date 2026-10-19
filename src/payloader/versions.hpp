// PayLoader - Version order, slugs and pldmgr's own names, as pldmgr's web UI has them.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from pldmgr's frontend/src/utils/versions.js and helpers.js (v0.6.1),
// so PayLoader calls the same payloads "newer" and "installed" as pldmgr does.

#pragma once

#include <string>
#include <string_view>

namespace hui::payloader
{

// 1 when a is newer than b, -1 when older, 0 when the same. Leading v/V is
// ignored; digit runs compare as numbers, letter runs without case; "1.7" is
// newer than "1.7beta3" and older than "1.7.1". Equal versions fall back to
// the last_update dates when both are given.
int compare_versions(std::string_view a, std::string_view b, std::string_view a_date = {},
                     std::string_view b_date = {});

// pldmgr's id for a name without one: "PS5 FTP-Server" -> "ps5-ftp-server".
std::string slug(std::string_view name);

// pldmgr itself, in any of the names it goes by.
bool is_system_payload(std::string_view name);

// Whether version is major.minor.patch or newer. False for text that is not a version.
bool version_at_least(std::string_view version, int major, int minor, int patch);

} // namespace hui::payloader
