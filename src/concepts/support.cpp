// PayLoader - Screen "Support": a Ko-fi QR code, for those who like what PayLoader does.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The code is a PNG in assets/support. The app hands this screen a loader for it (GL work, which
// screens leave to the app; see set_support_qr), used once when the screen is first drawn. The
// screen needs nothing from pldmgr, so it shows while pldmgr is away.

#include "concepts/concepts.hpp"

#include "concepts/payloader/screen.hpp"

#include <string>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace payloader;
using hui::payloader::Topic;
using hui::payloader::View;

constexpr const char *kLink = "ko-fi.com/H5L629ZHNX";

constexpr const char *kTechniques[] = {
    "The QR code is a PNG in the app's assets, decoded once with stb_image into a texture",
    "It leads to ko-fi.com/H5L629ZHNX; nothing is sent from the console",
    "Needs nothing from pldmgr: it shows while pldmgr is away",
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "support"},
};

// Set by the app: how to turn the PNG into a texture, and where the PNG is.
std::uint32_t (*g_load_png)(const std::string &path) = nullptr;
std::string g_qr_path;

class SupportScreen final : public Screen
{
  public:
    using Screen::Screen;

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "support",
            "Support",
            "Buy me a coffee or a beer",
            "src/concepts/support.cpp",
            audio::SoundSet::paper,
            Color::rgb(0xff5e5b),
            kTechniques,
        };
        return kInfo;
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  protected:
    const char *title() const override
    {
        return "Support";
    }
    const char *summary() const override
    {
        return "Buy me a coffee or a beer";
    }
    std::span<const Topic> topics() const override
    {
        return {};
    }
    std::span<const ui::Hint> hints() const override
    {
        return {};
    }
    // A QR code and some text: shown with or without pldmgr.
    bool usable(const View & /*view*/) const override
    {
        return true;
    }

    void show(const View & /*view*/) override
    {
    }

    void activated(int /*id*/, app::Feedback & /*feedback*/) override
    {
    }

    void draw_side(ui::Painter &paint) const override
    {
        const ui::Theme &look = paint.theme();
        if (!tried_)
        {
            tried_ = true;
            texture_ = g_load_png != nullptr ? g_load_png(g_qr_path) : 0;
        }

        constexpr float kQrSize = 560.0f;
        const Rect qr{kMargin, kTop + 20.0f, kQrSize, kQrSize};
        if (texture_ != 0)
            paint.list().image(texture_, qr, gfx::kFullUv, Color::rgb(0xffffff), 24.0f);
        else
            paint.fill(qr, 24.0f, look.surface);

        const float x = kMargin + kQrSize + 80.0f;
        const float width = kRight - x;
        float y = kTop + 120.0f;
        paint.heading("Enjoying PayLoader?", x, y, 56.0f, paint.page_text());
        y += 80.0f;
        y = paragraph(paint,
                      "If you like what I make, you can buy me a coffee or a beer. Scan the code "
                      "with your phone. Thank you!",
                      x, y, 30.0f, width, paint.page_text_muted());
        y += 70.0f;
        paint.label("KO-FI", x, y, 20.0f, paint.page_text_muted());
        y += 52.0f;
        paint.body(kLink, x, y, 34.0f, look.accent);
        if (texture_ == 0)
        {
            y += 70.0f;
            paint.body("The code could not be loaded; the link above leads to the same page.", x, y,
                       22.0f, paint.page_text_muted());
        }
    }

  private:
    mutable bool tried_ = false;
    mutable std::uint32_t texture_ = 0;
};

} // namespace

void set_support_qr(std::uint32_t (*load_png)(const std::string &path), std::string path)
{
    g_load_png = load_png;
    g_qr_path = std::move(path);
}

std::unique_ptr<app::Concept> make_support(app::Context &context)
{
    return std::make_unique<SupportScreen>(context);
}

} // namespace hui::concepts
