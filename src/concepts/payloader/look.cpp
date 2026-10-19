// PayLoader - What the screens share: the theme, the header, hints and the "no pldmgr" card.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/payloader/look.hpp"

#include "payloader/launcher.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace hui::concepts::payloader
{

using gfx::Color;
using gfx::Rect;
using hui::payloader::Link;
using hui::payloader::View;

const ui::Theme &theme()
{
    static const ui::Theme kTheme = []
    {
        for (const ui::Theme &candidate : ui::themes())
        {
            if (std::string_view(candidate.id) == "candy")
                return candidate;
        }
        return ui::default_theme();
    }();
    return kTheme;
}

void begin_frame(app::Frame &frame, const ui::Theme &look)
{
    frame.backdrop = look.backdrop;
}

void draw_header(ui::Painter &paint, const char *title, const char *summary, const View &view,
                 float clock)
{
    const ui::Theme &look = paint.theme();
    const Color ink = paint.page_text();
    const Color quiet = paint.page_text_muted();
    paint.label("PAYLOADER", kMargin, 104.0f, 19.0f, quiet);
    paint.heading(title, kMargin - 2.0f, 172.0f, 58.0f, ink);
    paint.body(summary, kMargin, 214.0f, 23.0f, quiet);

    // pldmgr's state as a pill: blue when it answers, pink when not, a soft pulse while asking.
    const char *state = "LOOKING FOR PLDMGR";
    Color fill = look.surface_high;
    Color text = look.text;
    switch (view.link)
    {
    case Link::ready:
        state = "PLDMGR RUNNING";
        fill = look.accent;
        text = Color::rgb(0xffffff);
        break;
    case Link::offline:
        state = "PLDMGR NOT RUNNING";
        fill = look.primary;
        text = look.on_primary;
        break;
    case Link::too_old:
        state = "PLDMGR TOO OLD";
        fill = look.primary;
        text = look.on_primary;
        break;
    case Link::unknown:
        state = "NOT PLDMGR ON 8084";
        fill = look.primary;
        text = look.on_primary;
        break;
    case Link::checking:
        fill = gfx::mix(look.surface_high, look.surface, 0.5f + 0.5f * std::sin(clock * 5.0f));
        break;
    }
    const float width = paint.label_width(state, 22.0f) + 80.0f;
    const Rect pill{kRight - width, 92.0f, width, 58.0f};
    paint.surface(pill, 29.0f, fill, look.outline, 0.6f);
    paint.fill({pill.x + 24.0f, pill.cy() - 7.0f, 14.0f, 14.0f}, 7.0f, text);
    paint.label(state, pill.x + 52.0f, pill.cy() + 8.0f, 22.0f, text);
    if (!view.version.empty() && view.link != Link::offline)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "pldmgr %s on 127.0.0.1:%d", view.version.c_str(),
                      hui::payloader::kPldmgrPort);
        paint.body(line, kRight, 196.0f, 21.0f, quiet, gfx::Align::right);
    }
}

void draw_hints(gfx::DrawList &list, const ui::Fonts &fonts, const ui::Theme &look,
                std::span<const ui::Hint> hints)
{
    ui::Hint row[10];
    int count = 0;
    for (const ui::Hint &hint : hints)
    {
        if (count < 9)
            row[count++] = hint;
    }
    row[count++] = {ui::Button::l1, "Screen", ui::Button::r1};
    ui::GlyphStyle glyphs = look.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
    glyphs.label = look.page_text.a > 0.0f ? look.page_text : look.text;
    ui::draw_hints(list, fonts, glyphs, row, count, kRight, true);
}

float paragraph(ui::Painter &paint, std::string_view text, float x, float y, float size,
                float width, Color color, int max_lines, float leading)
{
    const ui::FontRef &font = paint.font(ui::FontRole::regular);
    int lines = 0;
    for (const std::string &line : font.font->wrap(text, size, width))
    {
        if (max_lines > 0 && lines == max_lines)
            break;
        paint.body(line, x, y, size, color);
        y += size * leading;
        ++lines;
    }
    return y;
}

Rect side_panel(ui::Painter &paint, const Rect &r)
{
    const ui::Theme &look = paint.theme();
    return paint.surface(r, look.radius_card, look.surface, look.outline, 1.0f);
}

void draw_link_card(ui::Painter &paint, const Rect &r, const View &view)
{
    const ui::Theme &look = paint.theme();
    const Rect inside = side_panel(paint, r);
    const float x = inside.x + 48.0f;
    const float width = inside.w - 96.0f;
    float y = inside.y + 84.0f;
    if (view.link == Link::checking)
    {
        paint.heading("Looking for pldmgr", x, y, 38.0f, look.text);
        paragraph(paint, "Asking 127.0.0.1:8084 whether Payload Manager runs.", x, y + 56.0f, 25.0f,
                  width, look.text_muted);
        return;
    }
    const hui::payloader::BundledPldmgr &bundled = hui::payloader::bundled_pldmgr();
    if (view.pldmgr_starting)
    {
        paint.heading("Starting pldmgr", x, y, 38.0f, look.text);
        paragraph(paint, "Sent to the ELF loader; waiting for it to answer on 127.0.0.1:8084.", x,
                  y + 56.0f, 25.0f, width, look.text_muted);
        return;
    }
    if (view.link == Link::offline && bundled.present)
    {
        paint.heading("pldmgr is not running", x, y, 38.0f, look.text);
        y = paragraph(
            paint,
            "PayLoader works through Payload Manager (pldmgr) by itsPLK, and brings its own "
            "copy, version " +
                bundled.version +
                ". Press X to start it. pldmgr's own icon and browser start stay off: "
                "PayLoader is its front-end.",
            x, y + 56.0f, 25.0f, width, look.text_muted);
        paragraph(paint,
                  "pldmgr does not start with the console by itself; PayLoader asks again after a "
                  "restart. Once running, pldmgr keeps itself up to date (Repository screen).",
                  x, y + 18.0f, 23.0f, width, look.text_muted);
        return;
    }
    const char *title = "Start pldmgr first";
    std::string body =
        "PayLoader is a controller front-end for itsPLK's Payload Manager (pldmgr). It does not "
        "answer on this console yet.";
    if (view.link == Link::too_old)
    {
        title = "pldmgr needs an update";
        body = "This console runs pldmgr " + view.version +
               ". PayLoader speaks the API of version 0.6.0 and newer.";
    }
    else if (view.link == Link::unknown)
    {
        title = "Something else uses port 8084";
        body = "A server answers on 127.0.0.1:8084, but it is not pldmgr.";
    }
    paint.heading(title, x, y, 38.0f, look.text);
    y = paragraph(paint, body, x, y + 56.0f, 25.0f, width, look.text_muted);

    const char *const kSteps[] = {
        ("Get pldmgr.elf from github.com/itsPLK/ps5-payload-manager, or use an autoloader that "
         "includes it (Y2JB, BD-JB, Lua)."),
        "Load it with your autoloader, or send it to your ELF loader (etaHEN: port 9021).",
        "This screen checks again every 3 seconds, or press X to check now.",
    };
    y += 24.0f;
    int number = 1;
    for (const char *step : kSteps)
    {
        const Rect badge{x, y - 30.0f, 44.0f, 44.0f};
        paint.fill(badge, 22.0f, look.surface_high);
        char text[4];
        std::snprintf(text, sizeof(text), "%d", number++);
        paint.label(text, badge.cx(), badge.cy() + 8.0f, 22.0f, look.text, gfx::Align::center);
        y = paragraph(paint, step, x + 66.0f, y, 25.0f, width - 66.0f, look.text) + 16.0f;
    }
}

std::string memory_text(double mib)
{
    char text[32];
    if (mib >= 1024.0)
        std::snprintf(text, sizeof(text), "%.1f GB", mib / 1024.0);
    else if (mib >= 100.0)
        std::snprintf(text, sizeof(text), "%.0f MB", mib);
    else
        std::snprintf(text, sizeof(text), "%.1f MB", mib);
    return text;
}

} // namespace hui::concepts::payloader
