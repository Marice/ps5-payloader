// PayLoader - What the screens share: the theme, the header, hints and the "no pldmgr" card.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"
#include "payloader/store.hpp"
#include "ui/components/component.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <span>
#include <string_view>

namespace hui::concepts::payloader
{

constexpr float kMargin = 96.0f;
constexpr float kRight = gfx::kVirtualWidth - kMargin;
constexpr float kTop = 290.0f;    // where a screen's content starts
constexpr float kBottom = 940.0f; // ... and ends, above the hints
constexpr float kSplit = 1010.0f; // the side panel's left edge

// The app's one look: playful pastel ("09 · Candy" of the kit).
const ui::Theme &theme();

// Paints the page and returns nothing; everything else draws on frame.scene.
void begin_frame(app::Frame &frame, const ui::Theme &theme);

// "PAYLOADER", the screen's title and summary, and on the right whether
// pldmgr answers.
void draw_header(ui::Painter &paint, const char *title, const char *summary,
                 const hui::payloader::View &view, float clock);

// The hint row at the bottom right; L1/R1 is added.
void draw_hints(gfx::DrawList &list, const ui::Fonts &fonts, const ui::Theme &theme,
                std::span<const ui::Hint> hints);

// Explains how to get pldmgr running (or a newer one), for every screen.
void draw_link_card(ui::Painter &paint, const gfx::Rect &r, const hui::payloader::View &view);

// Running text wrapped to a width; returns the baseline after the last line.
float paragraph(ui::Painter &paint, std::string_view text, float x, float y, float size,
                float width, gfx::Color color, int max_lines = 0, float leading = 1.4f);

// A side panel's surface; returns its inside.
gfx::Rect side_panel(ui::Painter &paint, const gfx::Rect &r);

// "4.2 MB", "512 MB", "1.3 GB".
std::string memory_text(double mib);

} // namespace hui::concepts::payloader
