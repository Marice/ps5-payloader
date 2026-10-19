// PayLoader - Screen "Autoload": what pldmgr starts by itself, in which order.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr's own start list: on or off, the countdown before it, and the
// payloads with waits between them. Cross on an entry opens its menu (move,
// change a wait, remove). Every change is saved at once; until pldmgr's
// config shows it, the screen keeps its own copy, so a poll in between does
// not undo an edit. While pldmgr runs the list, the screen shows its progress
// and can stop it.

#include "concepts/concepts.hpp"

#include "concepts/payloader/screen.hpp"
#include "payloader/autoload_list.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
using hui::payloader::AutoloadState;
using hui::payloader::AutoloadStatus;
using hui::payloader::Payload;
using hui::payloader::Topic;
using hui::payloader::View;

enum Row
{
    kProgress = 1,
    kAbort,
    kEnabled,
    kDelay,
    kAddPayload,
    kAddWait,
    kEmpty,
    kChainInfo,
    kPldmgrList,
    kFirstEntry = 100, // + the entry's index
};

enum MenuTag
{
    kUp = 1,
    kDown,
    kRemove,
    kFirstPayload = 1000, // + the index into payloads_ for "Add a payload"
    kFirstFile = 2000,    // + the index into files_ (ps5-autoloader's folder)
};

constexpr float kWaitMin = 0.5f; // seconds, the wait sliders' range and step
constexpr float kWaitMax = 25.0f;
constexpr float kWaitStep = 0.5f;
constexpr float kSettleSeconds = 6.0f; // how long an unconfirmed save keeps the screen's copy
constexpr float kDelayQuiet = 1.0f;    // the countdown stepper saves after this pause

constexpr Topic kTopics[] = {Topic::autoload, Topic::payloads};

constexpr const char *kTechniques[] = {
    "pldmgr keeps the list (/data/pldmgr/autoload.txt) and runs it when it starts",
    ("Every change is saved at once with /set_config; the screen keeps its copy until pldmgr "
     "shows it"),
    "Progress comes from /autoload_status every second; Stop sends /abort",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Choose"},
    {ui::Button::triangle, "Options"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "autoload"},
    {0.3f, 0, Direction::down},
    {0.3f, 0, Direction::down},
    {0.6f, 0, Direction::none, "autoload-entry"},
    {0.5f, action_bit(Action::confirm), Direction::none},
    {0.6f, 0, Direction::none, "autoload-menu"},
    {0.3f, action_bit(Action::back), Direction::none},
};

const char *state_text(AutoloadState state)
{
    switch (state)
    {
    case AutoloadState::countdown:
        return "Counting down";
    case AutoloadState::executing:
        return "Starting payloads";
    case AutoloadState::waiting:
        return "Waiting for pldmgr's page";
    case AutoloadState::done:
        return "Done";
    case AutoloadState::aborted:
        return "Stopped";
    case AutoloadState::failed:
        return "Failed";
    case AutoloadState::disabled:
        return "Off";
    case AutoloadState::unknown:
        break;
    }
    return "-";
}

bool running(AutoloadState state)
{
    return state == AutoloadState::countdown || state == AutoloadState::executing ||
           state == AutoloadState::waiting;
}

class AutoloadScreen final : public Screen
{
  public:
    using Screen::Screen;

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "autoload",
            "Autoload",
            "The payloads pldmgr starts by itself, in order, with waits between them",
            "src/concepts/autoload.cpp",
            audio::SoundSet::paper,
            Color::rgb(0x9a6bff),
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
        return "Autoload";
    }
    const char *summary() const override
    {
        return "What starts with the console, in which order";
    }
    bool usable(const View &view) const override
    {
        return view.link == hui::payloader::Link::ready || file_chain(view);
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
        file_mode_ = file_chain(view);
        if (file_mode_)
        {
            show_file_chain(view);
            return;
        }
        if (!view.have_config)
            return;
        payloads_ = view.payloads.items;
        // pldmgr's copy, unless a save of ours has not shown up there yet.
        const bool confirmed = view.config.autoload_list == pending_text_ &&
                               view.config.autoload_enabled == pending_enabled_ &&
                               view.config.autoload_delay == pending_delay_;
        if (pending_ && (confirmed || clock_ - pending_since_ > kSettleSeconds))
            pending_ = false;
        const bool editing = settle_waits();
        if (!pending_ && !editing)
        {
            list_ = hui::payloader::parse_autoload_list(view.config.autoload_list);
            enabled_ = view.config.autoload_enabled;
            if (delay_changed_at_ < 0.0f)
                delay_ = view.config.autoload_delay;
        }
        if (delay_changed_at_ >= 0.0f && clock_ - delay_changed_at_ > kDelayQuiet)
        {
            delay_changed_at_ = -1.0f;
            save();
        }

        const bool active = view.have_autoload && running(view.autoload.state);
        std::string signature = (active ? "A" : "-") + std::to_string(enabled_) + "|";
        for (const AutoloadEntry &e : list_)
            signature += (e.wait ? std::string("!") : e.target) + ",";
        for (const Payload &p : payloads_)
            signature += p.slot + "/";
        if (signature != signature_)
        {
            signature_ = signature;
            rebuild(active);
        }
        sync_wait_sliders();
        form_.set_toggle(kEnabled, enabled_);
        if (delay_changed_at_ < 0.0f)
            form_.set_stepper(kDelay, delay_);
        if (active)
        {
            const AutoloadStatus &s = view.autoload;
            char text[96];
            if (s.state == AutoloadState::countdown)
                std::snprintf(text, sizeof(text), "Starts in %lld s",
                              (s.remaining_ms + 999) / 1000);
            else
                std::snprintf(text, sizeof(text), "%d of %d", s.done, s.total);
            form_.set_value_text(kProgress, text);
        }
    }

    void activated(int id, app::Feedback &feedback) override
    {
        switch (id)
        {
        case kAbort:
            hui::payloader::store().abort_autoload();
            return;
        case kAddPayload:
            open_add_menu(feedback);
            return;
        case kAddWait:
            list_.push_back(AutoloadEntry::pause(1000));
            save();
            return;
        default:
            break;
        }
        open_entry_menu(id, feedback);
    }

    void north(int id, app::Feedback &feedback) override
    {
        open_entry_menu(id, feedback);
    }

    // Move up, move down, remove: for a payload (Cross or Triangle) and a wait (Triangle; its
    // left and right change the time).
    void open_entry_menu(int id, app::Feedback &feedback)
    {
        const int index = id - kFirstEntry;
        if (index < 0 || index >= static_cast<int>(list_.size()))
            return;
        focused_entry_ = index;
        std::vector<ui::MenuItem> items;
        ui::MenuItem up = menu_item("Move up", kUp);
        up.disabled = index == 0;
        ui::MenuItem down = menu_item("Move down", kDown);
        down.disabled = index + 1 >= static_cast<int>(list_.size());
        items = {up, down};
        ui::MenuItem line;
        line.separator = true;
        ui::MenuItem remove = menu_item("Remove from the list", kRemove);
        remove.danger = true;
        items.push_back(line);
        items.push_back(remove);
        open_menu(std::move(items), feedback);
    }

    void changed(int id) override
    {
        if (id == kEnabled)
        {
            enabled_ = form_.toggle_value(kEnabled);
            save();
        }
        else if (id == kDelay)
        {
            delay_ = form_.stepper_value(kDelay);
            delay_changed_at_ = clock_;
        }
        else
        {
            const int index = id - kFirstEntry;
            if (index >= 0 && index < static_cast<int>(list_.size()) &&
                list_[static_cast<std::size_t>(index)].wait)
            {
                // Saved once the slider rests (kDelayQuiet), not at every step.
                list_[static_cast<std::size_t>(index)].wait_ms =
                    static_cast<int>(form_.slider_value(id) * 1000.0f + 0.5f);
                wait_changed_at_ = clock_;
            }
        }
    }

    void menu_chosen(int tag, app::Feedback & /*feedback*/) override
    {
        if (tag >= kFirstFile)
        {
            const std::size_t i = static_cast<std::size_t>(tag - kFirstFile);
            if (i < files_.size())
            {
                if (!list_.empty() && !list_.back().wait)
                    list_.push_back(AutoloadEntry::pause(3000));
                list_.push_back(AutoloadEntry::payload(files_[i]));
                save();
            }
            return;
        }
        if (tag >= kFirstPayload)
        {
            const std::size_t i = static_cast<std::size_t>(tag - kFirstPayload);
            if (i < payloads_.size())
            {
                if (file_mode_)
                {
                    hui::payloader::store().add_to_chain(payloads_[i]);
                    return;
                }
                list_.push_back(AutoloadEntry::payload(payloads_[i].slot));
                save();
            }
            return;
        }
        const std::size_t index = static_cast<std::size_t>(focused_entry_);
        if (index >= list_.size())
            return;
        switch (tag)
        {
        case kUp:
        case kDown:
            if (hui::payloader::move_entry(list_, index, tag == kUp ? -1 : 1))
            {
                save();
                form_.focus_row(kFirstEntry + focused_entry_ + (tag == kUp ? -1 : 1));
            }
            return;
        case kRemove:
            list_.erase(list_.begin() + static_cast<std::ptrdiff_t>(index));
            save();
            return;
        default:
            return;
        }
    }

    void draw_side(ui::Painter &paint) const override
    {
        const ui::Theme &look = paint.theme();
        const Rect inside = side_panel(paint, kSide);
        const float x = inside.x + 44.0f;
        const float width = inside.w - 88.0f;
        float y = inside.y + 64.0f;
        const View &now = view();
        if (file_mode_)
        {
            draw_file_side(paint, now, x, y, width);
            return;
        }
        paint.label("LAST RUN", x, y, 19.0f, look.text_muted);
        y += 58.0f;
        if (!now.have_autoload)
        {
            paint.body("-", x, y, 24.0f, look.text);
            return;
        }
        const AutoloadStatus &s = now.autoload;
        paint.heading(state_text(s.state), x, y, 36.0f, look.text);
        y += 46.0f;
        char line[96];
        std::snprintf(line, sizeof(line), "%d started, %d failed, %d skipped", s.successful,
                      s.failed, s.skipped);
        paint.body(line, x, y, 23.0f, look.text_muted);
        y += 50.0f;
        // One dot per payload of the run: green started, pink failed, grey skipped or to come.
        float dot_x = x;
        for (const char outcome : s.outcomes)
        {
            Color c = look.surface_high;
            if (outcome == 's')
                c = Color::rgb(0x36d67e);
            else if (outcome == 'f')
                c = look.primary;
            paint.fill({dot_x, y - 18.0f, 22.0f, 22.0f}, 11.0f, c);
            dot_x += 32.0f;
            if (dot_x > x + width - 22.0f)
                break;
        }
        if (!s.outcomes.empty())
            y += 40.0f;
        if (!s.current.empty() && running(s.state))
        {
            paint.label("NOW", x, y + 10.0f, 17.0f, look.text_muted);
            paint.body(title_of(s.current), x, y + 44.0f, 23.0f, look.text);
            y += 84.0f;
        }
        paragraph(paint,
                  "pldmgr runs this list each time it starts, after the countdown. When it does "
                  "not open the browser at start (Settings), the PS button skips the list during "
                  "the countdown.",
                  x, y + 20.0f, 22.0f, width, look.text_muted);
    }

  private:
    // ps5-autoloader's list is the chain: the autoloader found it, so it runs after the exploit.
    static bool file_chain(const View &view)
    {
        return view.have_chain && view.chain.kind == hui::payloader::ChainKind::autoloader;
    }

    void show_file_chain(const View &view)
    {
        payloads_ = view.payloads.items;
        files_ = view.chain_list.files;
        if (pending_ &&
            (view.chain_list.text == pending_text_ || clock_ - pending_since_ > kSettleSeconds))
            pending_ = false;
        const bool editing = settle_waits();
        if (!pending_ && !editing)
            list_ = view.chain_list.entries;
        std::string signature = "F" + view.chain.directory + "|";
        for (const AutoloadEntry &e : list_)
            signature += (e.wait ? std::string("!") : e.target) + ",";
        for (const std::string &f : files_)
            signature += f + "/";
        for (const Payload &p : payloads_)
            signature += p.slot + "/";
        if (view.chain.also_pldmgr && view.have_config)
            signature += view.config.autoload_list + (view.config.autoload_enabled ? "+" : "-");
        if (signature != signature_)
        {
            signature_ = signature;
            rebuild_file_chain(view);
        }
        sync_wait_sliders();
    }

    void rebuild_file_chain(const View &view)
    {
        begin_rebuild();
        form_.add_header("After the exploit");
        ui::FormRow &where = form_.add_value(kChainInfo, "ps5-autoloader", view.chain.directory);
        where.description = "The list the autoloader finds first; it starts these in order";
        form_.add_header("In this order");
        if (list_.empty())
            form_.add_value(kEmpty, "The list is empty", "");
        int number = 1;
        bool after_etahen = false;
        for (std::size_t i = 0; i < list_.size(); ++i)
        {
            const AutoloadEntry &entry = list_[i];
            const int id = kFirstEntry + static_cast<int>(i);
            if (entry.wait)
            {
                add_wait_row(id, entry,
                             after_etahen ? "etaHEN needs this time to start; a shorter wait "
                                            "can break the chain"
                                          : "A pause before the next payload");
                after_etahen = false;
                continue;
            }
            char label[160];
            std::snprintf(label, sizeof(label), "%d. %s", number++,
                          file_title(entry.target).c_str());
            ui::FormRow &row = form_.add_action(id, label);
            row.description = entry.target;
            if (std::find(files_.begin(), files_.end(), entry.target) == files_.end())
                row.text = "File missing";
            after_etahen = lower(entry.target).find("etahen") != std::string::npos;
        }
        form_.add_action(kAddPayload, "Add a payload").chevron = true;
        form_.add_action(kAddWait, "Add a wait").description = "One second; change it afterwards";
        if (view.chain.also_pldmgr)
        {
            form_.add_header("Then pldmgr's own list");
            const auto pldmgr_list = hui::payloader::parse_autoload_list(view.config.autoload_list);
            int count = 0;
            for (const AutoloadEntry &e : pldmgr_list)
                count += e.wait ? 0 : 1;
            ui::FormRow &row = form_.add_value(kPldmgrList, "pldmgr starts",
                                               !view.have_config ? std::string("-")
                                               : view.config.autoload_enabled
                                                   ? std::to_string(count) + " payloads"
                                                   : std::string("nothing (its autoload is off)"));
            row.description = "pldmgr is in the list above, so its own list runs too";
        }
        end_rebuild();
    }

    void draw_file_side(ui::Painter &paint, const View &now, float x, float y, float width) const
    {
        const ui::Theme &look = paint.theme();
        paint.label("BOOT LIST", x, y, 19.0f, look.text_muted);
        y += 58.0f;
        paint.heading("ps5-autoloader", x, y, 36.0f, look.text);
        y += 44.0f;
        paint.body(now.chain.directory + "/autoload.txt", x, y, 21.0f, look.text_muted);
        y += 50.0f;
        std::string text =
            "Your exploit starts ps5-autoloader, which starts this list. Changes take effect the "
            "next time the console starts. Before the first change of a day, PayLoader keeps the "
            "list as autoload.txt.bak-payloader-<date> next to it.";
        if (now.chain.usb_list)
            text += " This list is on a USB drive: it goes before the one in /data.";
        if (!now.chain.title_folders.empty())
            text += " There are also lists for single apps (ps5_autoloader_<title>); PayLoader "
                    "edits the general one.";
        if (!now.chain.also_pldmgr)
            text += " pldmgr's own list is not used on this console.";
        paragraph(paint, text, x, y, 22.0f, width, look.text_muted);
    }

    // 0.5 to 25 seconds in steps of 0.5; a longer (or shorter) wait already in the list widens the
    // range instead of being cut.
    void add_wait_row(int id, const AutoloadEntry &entry, const char *description)
    {
        const float seconds = static_cast<float>(entry.wait_ms) / 1000.0f;
        ui::FormRow &row = form_.add_slider(id, "Wait", seconds, std::min(kWaitMin, seconds),
                                            std::max(kWaitMax, seconds), kWaitStep);
        row.unit = " s";
        row.decimals = 1;
        row.description = description;
    }

    // The sliders show the list (another app may have changed it), except while one is moved.
    void sync_wait_sliders()
    {
        if (wait_changed_at_ >= 0.0f)
            return;
        for (std::size_t i = 0; i < list_.size(); ++i)
            if (list_[i].wait)
                form_.set_slider(kFirstEntry + static_cast<int>(i),
                                 static_cast<float>(list_[i].wait_ms) / 1000.0f);
    }

    // A slider was moved: save after it rests. True while that save is still to come.
    bool settle_waits()
    {
        if (wait_changed_at_ < 0.0f)
            return false;
        if (clock_ - wait_changed_at_ > kDelayQuiet)
        {
            wait_changed_at_ = -1.0f;
            save();
        }
        return true;
    }

    static std::string lower(const std::string &text)
    {
        std::string out;
        for (const char c : text)
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    static std::string file_title(const std::string &file)
    {
        const std::size_t dot = file.rfind('.');
        return dot == std::string::npos || dot == 0 ? file : file.substr(0, dot);
    }

    std::string title_of(const std::string &target) const
    {
        for (const Payload &p : payloads_)
            if (p.slot == target || p.path == target)
                return p.title();
        const std::size_t slash = target.rfind('/');
        return slash == std::string::npos ? target : target.substr(slash + 1);
    }

    void rebuild(bool active)
    {
        begin_rebuild();
        if (active)
        {
            form_.add_header("Running now");
            form_.add_value(kProgress, "Progress", "-");
            form_.add_action(kAbort, "Stop the autoload").danger = true;
        }
        form_.add_header("When pldmgr starts");
        form_.add_toggle(kEnabled, "Start the list", enabled_).description =
            "pldmgr starts these payloads each time it starts";
        ui::FormRow &delay = form_.add_stepper(kDelay, "Countdown", delay_, 0, 3600);
        delay.description = "Seconds before the first payload; time to skip the list";
        delay.unit = " s";
        form_.add_header("In this order");
        if (list_.empty())
            form_.add_value(kEmpty, "The list is empty", "");
        int number = 1;
        for (std::size_t i = 0; i < list_.size(); ++i)
        {
            const AutoloadEntry &entry = list_[i];
            const int id = kFirstEntry + static_cast<int>(i);
            if (entry.wait)
            {
                add_wait_row(id, entry, "A pause before the next payload");
                continue;
            }
            char label[160];
            std::snprintf(label, sizeof(label), "%d. %s", number++, title_of(entry.target).c_str());
            ui::FormRow &row = form_.add_action(id, label);
            row.description = entry.target;
            if (!known(entry.target))
                row.text = "Not installed";
        }
        form_.add_action(kAddPayload, "Add a payload").chevron = true;
        form_.add_action(kAddWait, "Add a wait").description = "One second; change it afterwards";
        end_rebuild();
    }

    bool known(const std::string &target) const
    {
        for (const Payload &p : payloads_)
            if (p.slot == target || p.path == target)
                return true;
        return false;
    }

    void open_add_menu(app::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        if (file_mode_)
        {
            // The files already next to the list, then pldmgr's payloads (copied in).
            for (std::size_t i = 0; i < files_.size(); ++i)
                if (!hui::payloader::autoload_contains(list_, files_[i]))
                    items.push_back(
                        menu_item(file_title(files_[i]), kFirstFile + static_cast<int>(i)));
            for (std::size_t i = 0; i < payloads_.size(); ++i)
            {
                const Payload &p = payloads_[i];
                if (p.kind == "usb" || hui::payloader::autoload_contains(list_, p.filename) ||
                    std::find(files_.begin(), files_.end(), p.filename) != files_.end())
                    continue;
                items.push_back(
                    menu_item(p.title(), kFirstPayload + static_cast<int>(i), "pldmgr"));
            }
            if (items.empty())
            {
                toast(ui::StatusKind::info, "Nothing to add",
                      "Every payload in the folder is in the list already");
                return;
            }
            open_menu(std::move(items), feedback);
            return;
        }
        for (std::size_t i = 0; i < payloads_.size(); ++i)
        {
            const Payload &p = payloads_[i];
            if (p.kind == "usb" || hui::payloader::autoload_contains(list_, p.slot))
                continue;
            ui::MenuItem item = menu_item(p.title(), kFirstPayload + static_cast<int>(i));
            items.push_back(std::move(item));
        }
        if (items.empty())
        {
            toast(ui::StatusKind::info, "Nothing to add",
                  "Every installed payload is in the list already");
            return;
        }
        open_menu(std::move(items), feedback);
    }

    void save()
    {
        if (file_mode_)
        {
            std::string lines;
            if (!hui::payloader::format_autoload_lines(list_, lines))
            {
                toast(ui::StatusKind::warning, "Not saved", "The autoloader cannot read this list");
                return;
            }
            pending_ = true;
            pending_text_ = lines;
            pending_since_ = clock_;
            signature_.clear();
            hui::payloader::store().save_chain(list_);
            return;
        }
        std::string text;
        if (!hui::payloader::format_autoload_list(list_, text))
        {
            toast(ui::StatusKind::warning, "Not saved", "pldmgr cannot keep this list");
            return;
        }
        pending_ = true;
        pending_text_ = text;
        pending_enabled_ = enabled_;
        pending_delay_ = delay_;
        pending_since_ = clock_;
        signature_.clear();
        hui::payloader::store().save_autoload(enabled_, delay_, text);
    }

    std::vector<AutoloadEntry> list_;
    std::vector<Payload> payloads_;
    std::vector<std::string> files_;
    std::string signature_;
    std::string pending_text_;
    float pending_since_ = 0.0f;
    float delay_changed_at_ = -1.0f;
    float wait_changed_at_ = -1.0f;
    int delay_ = 5;
    int pending_delay_ = 5;
    bool pending_enabled_ = false;
    int focused_entry_ = 0;
    bool enabled_ = false;
    bool pending_ = false;
    bool file_mode_ = false;
};

} // namespace

std::unique_ptr<app::Concept> make_autoload(app::Context &context)
{
    return std::make_unique<AutoloadScreen>(context);
}

} // namespace hui::concepts
