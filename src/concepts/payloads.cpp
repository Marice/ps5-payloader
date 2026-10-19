// PayLoader - Screen "Payloads": what pldmgr has installed, started with one press.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Favourites first, then the rest pldmgr keeps, then what it finds on a USB
// drive. Cross starts the focused payload; Triangle opens its menu:
// favourite, start with the autoload list, delete. The side panel shows
// where it came from.

#include "concepts/concepts.hpp"

#include "concepts/payloader/ids.hpp"
#include "concepts/payloader/screen.hpp"
#include "payloader/autoload_list.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace payloader;
using hui::payloader::AutoloadEntry;
using hui::payloader::Payload;
using hui::payloader::Topic;
using hui::payloader::View;

enum MenuTag
{
    kStart = 1,
    kFavourite,
    kAutoload,
    kDelete,
};

enum Purpose
{
    kConfirmDelete = 1,
};

constexpr int kEmptyRow = 1;

constexpr Topic kTopics[] = {Topic::payloads};

constexpr const char *kTechniques[] = {
    "Reads pldmgr's /list_payloads and /get_config every 4 seconds while this screen shows",
    "Start sends /loadpayload: with the payload's slot; pldmgr hands it to the ELF loader",
    "Favourites and the autoload list are pldmgr's own, so its web page shows the same",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Start"},
    {ui::Button::triangle, "Options"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "payloads"},
    {0.4f, 0, Direction::down},
    {0.6f, 0, Direction::none, "payloads-focus"},
    {0.6f, action_bit(Action::north), Direction::none},
    {0.6f, 0, Direction::none, "payloads-menu"},
    {0.3f, action_bit(Action::back), Direction::none},
};

std::vector<std::string> split(const std::string &csv)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= csv.size())
    {
        std::size_t comma = csv.find(',', start);
        if (comma == std::string::npos)
            comma = csv.size();
        if (comma > start)
            out.push_back(csv.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

const char *kind_text(const Payload &p)
{
    if (p.kind == "usb")
        return "On a USB drive";
    if (p.kind == "local")
        return "Added by hand";
    return "From a repository";
}

class PayloadsScreen final : public Screen
{
  public:
    using Screen::Screen;

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "payloads",
            "Payloads",
            "Everything pldmgr has installed, started with one press",
            "src/concepts/payloads.cpp",
            audio::SoundSet::paper,
            Color::rgb(0xff6fae),
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
        return "Payloads";
    }
    const char *summary() const override
    {
        return "Start what pldmgr has installed; Triangle for favourites, autoload and delete";
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
        const std::vector<std::string> favourites = split(view.config.favorites);
        std::string signature = view.config.favorites + "|" + view.config.autoload_list;
        for (const Payload &p : view.payloads.items)
            signature += "|" + p.slot + ":" + p.version;
        if (signature != signature_ || !built_)
        {
            signature_ = signature;
            built_ = true;
            rebuild(view, favourites);
        }
        for (const auto &[id, slot] : ids_.entries())
        {
            FormRowText text = row_text(view, slot);
            if (ui::FormRow *row = form_.row(id))
                row->text = std::move(text.text);
            form_.set_disabled(id, text.busy);
        }
    }

    void activated(int id, app::Feedback &feedback) override
    {
        if (const Payload *p = payload(id))
        {
            hui::payloader::store().launch(*p);
            feedback.play(audio::Cue::launch);
        }
    }

    void north(int id, app::Feedback &feedback) override
    {
        const Payload *p = payload(id);
        if (p == nullptr)
            return;
        menu_slot_ = p->slot;
        const View &now = view();
        const std::vector<std::string> favourites = split(now.config.favorites);
        ui::MenuItem start = menu_item("Start", kStart);
        ui::MenuItem favourite = menu_item("Favourite", kFavourite);
        favourite.checkable = true;
        favourite.checked =
            std::find(favourites.begin(), favourites.end(), p->slot) != favourites.end();
        ui::MenuItem autoload = menu_item("Start with autoload", kAutoload);
        autoload.checkable = true;
        autoload.checked = in_chain(now, *p);
        ui::MenuItem line;
        line.separator = true;
        ui::MenuItem remove = menu_item("Delete", kDelete);
        remove.danger = true;
        remove.disabled = p->kind == "usb";
        open_menu({start, favourite, autoload, line, remove}, feedback);
    }

    void menu_chosen(int tag, app::Feedback &feedback) override
    {
        const Payload *p = payload(ids_.id(menu_slot_));
        if (p == nullptr)
            return;
        const View &now = view();
        hui::payloader::Store &store = hui::payloader::store();
        switch (tag)
        {
        case kStart:
            store.launch(*p);
            break;
        case kFavourite:
        {
            const std::vector<std::string> favourites = split(now.config.favorites);
            const bool is_favourite =
                std::find(favourites.begin(), favourites.end(), p->slot) != favourites.end();
            store.set_favourite(p->slot, !is_favourite);
            break;
        }
        case kAutoload:
            toggle_autoload(*p);
            break;
        case kDelete:
            pending_delete_ = p->slot;
            ask(kConfirmDelete,
                {ui::StatusKind::danger,
                 "Delete " + p->title() + "?",
                 "pldmgr removes the file from the console. A payload from a repository can be "
                 "installed again.",
                 {{"Cancel"}, {"Delete", ui::ButtonKind::primary, true}},
                 0},
                feedback);
            break;
        default:
            break;
        }
    }

    void dialog_chosen(int purpose, int choice, app::Feedback & /*feedback*/) override
    {
        if (purpose != kConfirmDelete || choice != 1)
            return;
        if (const Payload *p = payload(ids_.id(pending_delete_)))
            hui::payloader::store().remove(*p);
    }

    void draw_side(ui::Painter &paint) const override
    {
        const ui::Theme &look = paint.theme();
        const Rect inside = side_panel(paint, kSide);
        const float x = inside.x + 44.0f;
        const float width = inside.w - 88.0f;
        float y = inside.y + 64.0f;
        const Payload *p = payload(form_.focus_id());
        if (p == nullptr)
        {
            paint.label("PAYLOADS", x, y, 19.0f, look.text_muted);
            paragraph(paint,
                      "Install payloads on the Repository screen, or add them in pldmgr's web "
                      "page. Payloads on a USB drive show here when pldmgr scans for them "
                      "(Settings).",
                      x, y + 50.0f, 24.0f, width, look.text);
            return;
        }
        paint.label(kind_text(*p), x, y, 19.0f, look.text_muted);
        y += 62.0f;
        paint.heading(p->title(), x, y, 40.0f, look.text);
        y += 50.0f;
        std::string line = p->version.empty() ? std::string() : "Version " + p->version;
        if (!p->author.empty())
            line += (line.empty() ? "By " : ", by ") + p->author;
        if (!line.empty())
        {
            paint.body(line, x, y, 23.0f, look.text_muted);
            y += 44.0f;
        }
        if (!p->description.empty())
            y = paragraph(paint, p->description, x, y, 23.0f, width, look.text, 5) + 10.0f;
        if (!p->source_name.empty())
        {
            paint.label("REPOSITORY", x, y + 10.0f, 17.0f, look.text_muted);
            paint.body(p->source_name, x, y + 44.0f, 22.0f, look.text);
            y += 84.0f;
        }
        // Where else pldmgr knows it from: the favourites and the autoload list.
        const View &now = view();
        const std::vector<std::string> favourites = split(now.config.favorites);
        const bool favourite =
            std::find(favourites.begin(), favourites.end(), p->slot) != favourites.end();
        int position = 0;
        int number = 0;
        const bool file_chain =
            now.have_chain && now.chain.kind == hui::payloader::ChainKind::autoloader;
        for (const AutoloadEntry &e :
             file_chain ? now.chain_list.entries
                        : hui::payloader::parse_autoload_list(now.config.autoload_list))
        {
            if (e.wait)
                continue;
            ++number;
            if (file_chain ? e.target == p->filename : (e.target == p->slot || e.target == p->path))
                position = number;
        }
        if (favourite || position > 0)
        {
            std::string status = favourite ? "Favourite" : "";
            if (position > 0)
                status += (status.empty() ? "" : ", ") + std::string("number ") +
                          std::to_string(position) + " at start-up";
            paint.body(status, x, y + 10.0f, 22.0f, look.accent);
            y += 50.0f;
        }
        paint.label("FILE", x, y + 10.0f, 17.0f, look.text_muted);
        paragraph(paint, p->path, x, y + 44.0f, 20.0f, width, look.text, 3, 1.35f);
    }

  private:
    struct FormRowText
    {
        std::string text;
        bool busy = false;
    };

    FormRowText row_text(const View &view, const std::string &slot) const
    {
        if (view.busy.count(slot) != 0)
            return {"Working", true};
        for (const Payload &p : view.payloads.items)
        {
            if (p.slot != slot)
                continue;
            if (p.kind == "usb")
                return {"USB"};
            if (p.kind == "local")
                return {};
            return {p.version.empty() ? std::string("Repository") : p.version};
        }
        return {};
    }

    void rebuild(const View &view, const std::vector<std::string> &favourites)
    {
        begin_rebuild();
        payloads_ = view.payloads.items;
        const auto is_favourite = [&](const Payload &p)
        { return std::find(favourites.begin(), favourites.end(), p.slot) != favourites.end(); };
        const auto add = [&](const Payload &p)
        {
            ui::FormRow &row = form_.add_action(ids_.id(p.slot), p.title());
            row.description = kind_text(p);
        };
        if (payloads_.empty())
        {
            form_.add_header("Installed");
            form_.add_value(kEmptyRow, "Nothing installed yet", "").description =
                "Install payloads on the Repository screen";
        }
        bool any = false;
        for (const Payload &p : payloads_)
        {
            if (!is_favourite(p))
                continue;
            if (!any)
                form_.add_header("Favourites");
            any = true;
            add(p);
        }
        any = false;
        for (const Payload &p : payloads_)
        {
            if (is_favourite(p) || p.kind == "usb")
                continue;
            if (!any)
                form_.add_header("Installed");
            any = true;
            add(p);
        }
        any = false;
        for (const Payload &p : payloads_)
        {
            if (is_favourite(p) || p.kind != "usb")
                continue;
            if (!any)
                form_.add_header("On a USB drive");
            any = true;
            add(p);
        }
        end_rebuild();
    }

    const Payload *payload(int id) const
    {
        const std::string *slot = ids_.slot(id);
        if (slot == nullptr)
            return nullptr;
        for (const Payload &p : payloads_)
            if (p.slot == *slot)
                return &p;
        return nullptr;
    }

    // Whether the payload starts with the console: in ps5-autoloader's list (by file name) when
    // that is the chain, else in pldmgr's own list (by slot).
    static bool in_chain(const View &now, const Payload &p)
    {
        if (now.have_chain && now.chain.kind == hui::payloader::ChainKind::autoloader)
            return hui::payloader::autoload_contains(now.chain_list.entries, p.path) ||
                   hui::payloader::autoload_contains(now.chain_list.entries, p.filename);
        return hui::payloader::autoload_contains(
            hui::payloader::parse_autoload_list(now.config.autoload_list), p.slot);
    }

    void toggle_autoload(const Payload &p)
    {
        const View &now = view();
        if (now.have_chain && now.chain.kind == hui::payloader::ChainKind::autoloader)
        {
            if (in_chain(now, p))
                hui::payloader::store().remove_from_chain(
                    hui::payloader::autoload_contains(now.chain_list.entries, p.path) ? p.path
                                                                                      : p.filename);
            else
                hui::payloader::store().add_to_chain(p);
            return;
        }
        std::vector<AutoloadEntry> list =
            hui::payloader::parse_autoload_list(now.config.autoload_list);
        const bool listed = hui::payloader::autoload_contains(list, p.slot);
        if (listed)
            list.erase(std::remove_if(list.begin(), list.end(), [&](const AutoloadEntry &e)
                                      { return !e.wait && e.target == p.slot; }),
                       list.end());
        else
            list.push_back(AutoloadEntry::payload(p.slot));
        std::string text;
        if (!hui::payloader::format_autoload_list(list, text))
        {
            toast(ui::StatusKind::warning, "Autoload not changed",
                  "pldmgr cannot keep a list this long");
            return;
        }
        hui::payloader::store().save_autoload(now.config.autoload_enabled,
                                              now.config.autoload_delay, text);
    }

    RowIds ids_;
    std::vector<Payload> payloads_;
    std::string signature_;
    std::string menu_slot_;
    std::string pending_delete_;
    bool built_ = false;
};

} // namespace

std::unique_ptr<app::Concept> make_payloads(app::Context &context)
{
    return std::make_unique<PayloadsScreen>(context);
}

} // namespace hui::concepts
