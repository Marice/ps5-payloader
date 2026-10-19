// PayLoader - A pretend pldmgr with a few payloads, for the PC preview.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host snapshots have no console and no pldmgr. This answers the routes
// PayLoader reads with fixed, made-up data and accepts every change, so each
// screen can be looked at with content in it.
//
//   install_store([] { return make_demo_transport(); });

#pragma once

#include "payloader/http.hpp"

#include <memory>

namespace hui::payloader
{

std::unique_ptr<Transport> make_demo_transport();

} // namespace hui::payloader
