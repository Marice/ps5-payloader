// ps5-homebrew-ui - Factories of every UI design in the app.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace hui::concepts
{

// One function per screen, each defined in its own file in this directory.
// To add one: write the file, declare its factory here and list it in
// registry.cpp. These are PayLoader's screens, in L1 / R1 order.
std::unique_ptr<app::Concept> make_payloads(app::Context &context);
std::unique_ptr<app::Concept> make_repository(app::Context &context);
std::unique_ptr<app::Concept> make_autoload(app::Context &context);
std::unique_ptr<app::Concept> make_running(app::Context &context);
std::unique_ptr<app::Concept> make_settings(app::Context &context);
std::unique_ptr<app::Concept> make_support(app::Context &context);
// The Support screen's QR code: a PNG and how to make it a texture (GL work, done by the app).
void set_support_qr(std::uint32_t (*load_png)(const std::string &path), std::string path);

} // namespace hui::concepts
