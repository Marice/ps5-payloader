// PayLoader - Screen "Running": the payloads that run now, and stopping one.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr lists every process; this screen shows only payloads (an ELF
// without an app), and never pldmgr itself, so a press cannot stop the
// system or the manager. Cross asks, then stops the focused payload.

#include "concepts/concepts.hpp"

#include "concepts/payloader/ids.hpp"
#include "concepts/payloader/screen.hpp"
#include "payloader/versions.hpp"

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
using hui::payloader::Process;
using hui::payloader::Topic;
using hui::payloader::View;

enum Purpose
{
    kConfirmStop = 1,
};

constexpr int kEmptyRow = 1;

constexpr Topic kTopics[] = {Topic::processes};

constexpr const char *kTechniques[] = {
    "Reads pldmgr's /processes_list every 3 seconds while this screen shows",
    "Only payloads are listed: ELF files with no app of their own, without pldmgr",
    "Stop sends /process_kill; pldmgr ends the payload, and forces it after a moment",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Stop"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "running"},
    {0.4f, action_bit(Action::confirm), Direction::none},
    {0.6f, 0, Direction::none, "running-stop"},
    {0.3f, action_bit(Action::back), Direction::none},
};

bool shown(const Process &p)
{
    return p.payload && !hui::payloader::is_system_payload(p.name);
}

class RunningScreen final : public Screen
{
  public:
    using Screen::Screen;

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "running",
            "Running",
            "The payloads that run now; stop one that misbehaves",
            "src/concepts/running.cpp",
            audio::SoundSet::paper,
            Color::rgb(0x36d67e),
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
        return "Running";
    }
    const char *summary() const override
    {
        return "The payloads that run now; stop one that misbehaves";
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
        std::string signature = view.have_processes ? "P" : "-";
        for (const Process &p : view.processes)
            if (shown(p))
                signature += "|" + std::to_string(p.pid);
        if (signature != signature_)
        {
            signature_ = signature;
            begin_rebuild();
            processes_.clear();
            form_.add_header("Payloads");
            for (const Process &p : view.processes)
            {
                if (!shown(p))
                    continue;
                processes_.push_back(p);
                ui::FormRow &row = form_.add_action(ids_.id(std::to_string(p.pid)), p.name);
                row.description = "Process " + std::to_string(p.pid);
            }
            if (processes_.empty())
                form_.add_value(kEmptyRow,
                                view.have_processes ? "No payload runs" : "Asking pldmgr", "");
            end_rebuild();
        }
        for (Process &p : processes_)
        {
            for (const Process &now : view.processes)
                if (now.pid == p.pid)
                    p.memory_mib = now.memory_mib;
            const int id = ids_.id(std::to_string(p.pid));
            const bool busy = view.busy.count("pid:" + std::to_string(p.pid)) != 0;
            if (ui::FormRow *row = form_.row(id))
                row->text = busy ? std::string("Stopping") : memory_text(p.memory_mib);
            form_.set_disabled(id, busy);
        }
    }

    void activated(int id, app::Feedback &feedback) override
    {
        const Process *p = find(id);
        if (p == nullptr)
            return;
        pending_ = *p;
        ask(kConfirmStop,
            {ui::StatusKind::warning,
             "Stop " + p->name + "?",
             "The payload ends at once. What it was doing stops with it; start it again on the "
             "Payloads screen.",
             {{"Cancel"}, {"Stop", ui::ButtonKind::primary, true}},
             0},
            feedback);
    }

    void dialog_chosen(int purpose, int choice, app::Feedback & /*feedback*/) override
    {
        if (purpose == kConfirmStop && choice == 1)
            hui::payloader::store().stop(pending_);
    }

    void draw_side(ui::Painter &paint) const override
    {
        const ui::Theme &look = paint.theme();
        const Rect inside = side_panel(paint, kSide);
        const float x = inside.x + 44.0f;
        const float width = inside.w - 88.0f;
        float y = inside.y + 64.0f;
        paint.label("RUNNING NOW", x, y, 19.0f, look.text_muted);
        y += 70.0f;
        double total = 0.0;
        for (const Process &p : processes_)
            total += p.memory_mib;
        char line[64];
        std::snprintf(line, sizeof(line), "%zu %s", processes_.size(),
                      processes_.size() == 1 ? "payload" : "payloads");
        paint.heading(line, x, y, 44.0f, look.text);
        y += 46.0f;
        paint.body(memory_text(total) + " of memory together", x, y, 23.0f, look.text_muted);
        y += 60.0f;
        paragraph(paint,
                  "Only payloads show here. The system's own processes and pldmgr itself are left "
                  "out, so nothing vital can be stopped from this screen.",
                  x, y, 22.0f, width, look.text_muted);
    }

  private:
    const Process *find(int id) const
    {
        for (const Process &p : processes_)
            if (ids_.slot(id) != nullptr && *ids_.slot(id) == std::to_string(p.pid))
                return &p;
        return nullptr;
    }

    RowIds ids_;
    std::vector<Process> processes_;
    std::string signature_ = "?";
    Process pending_;
};

} // namespace

std::unique_ptr<app::Concept> make_running(app::Context &context)
{
    return std::make_unique<RunningScreen>(context);
}

} // namespace hui::concepts
