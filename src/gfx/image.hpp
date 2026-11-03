// PayLoader - A texture from a PNG file.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GL work, so not for screens themselves: the app hands this to the screen that needs it (see
// concepts::set_support_qr).
//
//   std::uint32_t texture = gfx::load_png_texture("/app0/assets/support/ko-fi-qr.png");

#pragma once

#include <cstdint>
#include <string>

namespace hui::gfx
{

// The PNG decoded into an RGBA texture (one level, linear filtering); 0 when it can't be read.
std::uint32_t load_png_texture(const std::string &path);

} // namespace hui::gfx
