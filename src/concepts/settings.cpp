// PayLoader - Screen "Settings": pldmgr's repositories and its own options.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The repositories pldmgr reads (add one by its address, remove one), and the
// switches pldmgr keeps: extra repositories, its browser at start, its own
// home screen icon, the disc player, USB drives. Each switch is saved at once.

#include "concepts/concepts.hpp"

#include "concepts/payloader/ids.hpp"
#include "concepts/payloader/screen.hpp"

#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace payloader;
using hui::payloader::ConfigChange;
using hui::payloader::Source;
using hui::payloader::Topic;
using hui::payloader::View;

enum Row
{
    kMultiSources = 1,
    kAddSource,
    kBrowser,
    kIcon,
    kDiscPlayer,
    kUsb,
    kVersion,
    kAbout,
};

enum Purpose
{
    kConfirmRemove = 1,
    kSourceAddress = 2,
};

constexpr Topic kTopics[] = {Topic::sources};

constexpr const char *kTechniques[] = {
    "Repositories come from /sources_list; adding one sends its address to /sources_add",
    "The switches are pldmgr's own (/get_config, /set_config), saved at once",
    "PayLoader keeps no settings of its own",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Choose"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "settings"},
    {0.3f, 0, Direction::down},
    {0.3f, 0, Direction::down},
    {0.3f, 0, Direction::down},
    {0.6f, 0, Direction::none, "settings-focus"},
};

std::string host_of(const std::string &url)
{
    std::size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    const std::size_t end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

class SettingsScreen final : public Screen
{
  public:
    using Screen::Screen;

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "settings",
            "Settings",
            "pldmgr's repositories and its own options",
            "src/concepts/settings.cpp",
            audio::SoundSet::paper,
            Color::rgb(0xffb35c),
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
        return "Settings";
    }
    const char *summary() const override
    {
        return "pldmgr's repositories and its own options";
    }
    std::span<const Topic> topics() const override
    {
        return kTopics;
    }
    std::span<const ui::Hint> hints() const override
    {
        return kHints;
    }

    void show(const View &view) override
    {
        std::string signature = view.have_sources ? "S" : "-";
        for (const Source &s : view.sources)
            signature += "|" + s.url;
        if (signature != signature_)
        {
            signature_ = signature;
            rebuild(view);
        }
        if (view.have_config)
        {
            form_.set_toggle(kMultiSources, view.config.multi_sources);
            form_.set_toggle(kBrowser, view.config.auto_browser_open);
            form_.set_toggle(kIcon, view.config.auto_install_app);
            form_.set_toggle(kDiscPlayer, view.config.kill_disc_player);
            form_.set_toggle(kUsb, view.config.scan_usb);
        }
        form_.set_value_text(kVersion, view.version.empty() ? "-" : view.version);
        const bool adding = view.busy.count("source:add") != 0;
        if (ui::FormRow *row = form_.row(kAddSource))
            row->text = adding ? "Adding" : "";
        form_.set_disabled(kAddSource, adding);
    }

    void activated(int id, app::Feedback &feedback) override
    {
        if (id == kAddSource)
        {
            prompt(kSourceAddress, "Repository address", "https://example.com/payloads.json", 512,
                   feedback);
            return;
        }
        for (std::size_t i = 0; i < sources_.size(); ++i)
        {
            if (ids_.id(sources_[i].url) != id)
                continue;
            if (!sources_[i].removable)
            {
                toast(ui::StatusKind::info, "pldmgr's official repository stays");
                return;
            }
            pending_index_ = static_cast<int>(i);
            pending_name_ = sources_[i].name.empty() ? sources_[i].url : sources_[i].name;
            ask(kConfirmRemove,
                {ui::StatusKind::warning,
                 "Remove " + pending_name_ + "?",
                 "Its payloads no longer show on the Repository screen. Payloads you installed "
                 "from it stay.",
                 {{"Cancel"}, {"Remove", ui::ButtonKind::primary, true}},
                 0},
                feedback);
            return;
        }
    }

    void changed(int id) override
    {
        ConfigChange change;
        const bool on = form_.toggle_value(id);
        switch (id)
        {
        case kMultiSources:
            change.multi_sources = on;
            break;
        case kBrowser:
            change.auto_browser_open = on;
            break;
        case kIcon:
            change.auto_install_app = on;
            break;
        case kDiscPlayer:
            change.kill_disc_player = on;
            break;
        case kUsb:
            change.scan_usb = on;
            break;
        default:
            return;
        }
        hui::payloader::store().save_settings(change);
    }

    void dialog_chosen(int purpose, int choice, app::Feedback & /*feedback*/) override
    {
        if (purpose == kConfirmRemove && choice == 1)
            hui::payloader::store().remove_source(pending_index_, pending_name_);
    }

    void prompt_done(int purpose, const std::string &text, app::Feedback & /*feedback*/) override
    {
        if (purpose != kSourceAddress)
            return;
        if (text.rfind("http://", 0) != 0 && text.rfind("https://", 0) != 0)
        {
            toast(ui::StatusKind::warning, "Not added", "An address starts with https://");
            return;
        }
        if (text.find_first_of(" \t\"\\") != std::string::npos)
        {
            toast(ui::StatusKind::warning, "Not added", "An address has no spaces or quotes");
            return;
        }
        hui::payloader::store().add_source(text);
    }

    void draw_side(ui::Painter &paint) const override
    {
        const ui::Theme &look = paint.theme();
        const Rect inside = side_panel(paint, kSide);
        const float x = inside.x + 44.0f;
        const float width = inside.w - 88.0f;
        float y = inside.y + 64.0f;
        paint.label("ABOUT", x, y, 19.0f, look.text_muted);
        y += 58.0f;
        paint.heading("PayLoader", x, y, 40.0f, look.text);
        y += 50.0f;
        y = paragraph(paint,
                      "A controller front-end for Payload Manager (pldmgr) by itsPLK. pldmgr does "
                      "the work and keeps every setting; PayLoader shows it on the television.",
                      x, y, 23.0f, width, look.text);
        y += 16.0f;
        paragraph(paint,
                  "pldmgr's web page has no password and can be opened from any device on your "
                  "network. Use PayLoader and pldmgr on a network you trust.",
                  x, y, 22.0f, width, look.text_muted);
    }

  private:
    void rebuild(const View &view)
    {
        begin_rebuild();
        sources_ = view.sources;
        form_.add_header("Repositories");
        form_.add_toggle(kMultiSources, "Use extra repositories", view.config.multi_sources)
            .description = "Without it, pldmgr reads only its official repository";
        for (const Source &s : sources_)
        {
            ui::FormRow &row =
                form_.add_action(ids_.id(s.url), s.name.empty() ? host_of(s.url) : s.name);
            row.text = host_of(s.url);
            row.description = s.removable ? "Cross to remove it" : "pldmgr's official repository";
        }
        form_.add_action(kAddSource, "Add a repository").chevron = true;
        form_.add_header("pldmgr");
        form_.add_toggle(kBrowser, "Open the browser at start", view.config.auto_browser_open)
            .description = "pldmgr opens its web page when it starts; PayLoader does not need it";
        form_.add_toggle(kIcon, "pldmgr's home screen icon", view.config.auto_install_app)
            .description = "pldmgr adds a \"Payload Manager\" icon that opens its web page";
        form_
            .add_toggle(kDiscPlayer, "Close the disc player at start", view.config.kill_disc_player)
            .description = "pldmgr closes the console's disc player app when it starts";
        form_.add_toggle(kUsb, "Look on USB drives", view.config.scan_usb).description =
            "Payloads on a USB drive show on the Payloads screen";
        form_.add_value(kVersion, "pldmgr version", "-");
        end_rebuild();
    }

    RowIds ids_;
    std::vector<Source> sources_;
    std::string signature_ = "?";
    std::string pending_name_;
    int pending_index_ = 0;
};

} // namespace

std::unique_ptr<app::Concept> make_settings(app::Context &context)
{
    return std::make_unique<SettingsScreen>(context);
}

} // namespace hui::concepts
