// PayLoader - Screen "Repository": payloads to install, and updates for the ones you have.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr's repositories, each with its payloads. A row says whether the
// payload is installed, and from which version an update is ready. Cross
// installs or updates; Square refreshes every repository. An install that
// meets the same payload from another repository asks first (Screen does).

#include "concepts/concepts.hpp"

#include "concepts/payloader/ids.hpp"
#include "concepts/payloader/screen.hpp"
#include "payloader/repo_status.hpp"

#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace payloader;
using hui::payloader::InstallMode;
using hui::payloader::RepoPayload;
using hui::payloader::RepoSource;
using hui::payloader::RepoStatus;
using hui::payloader::Topic;
using hui::payloader::View;

enum Purpose
{
    kConfirmReinstall = 1,
};

constexpr int kRefreshRow = 1;
constexpr int kLoadingRow = 2;

constexpr Topic kTopics[] = {Topic::repository, Topic::payloads};

constexpr const char *kTechniques[] = {
    "Reads pldmgr's cached /repository_payloads once; Square asks it to fetch every repository",
    "Install and update go to /repository_install; pldmgr downloads and checks the payload",
    "Installed, update and conflict follow the same rules as pldmgr's own web page",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Install"},
    {ui::Button::square, "Refresh"},
};

constexpr app::TourStep kTour[] = {
    {1.4f, 0, Direction::none, "repository"},
    {0.3f, 0, Direction::down},
    {0.3f, 0, Direction::down},
    {0.6f, 0, Direction::none, "repository-focus"},
};

std::string first_line(const std::string &text)
{
    const std::size_t end = text.find('\n');
    return end == std::string::npos ? text : text.substr(0, end);
}

class RepositoryScreen final : public Screen
{
  public:
    using Screen::Screen;

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "repository",
            "Repository",
            "Payloads to install from pldmgr's repositories, and their updates",
            "src/concepts/repository.cpp",
            audio::SoundSet::paper,
            Color::rgb(0x57c8ff),
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
        return "Repository";
    }
    const char *summary() const override
    {
        return "Install payloads and their updates from pldmgr's repositories";
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
        std::string signature = view.have_repository ? "R" : "-";
        for (const RepoSource &source : view.repository)
        {
            signature += "|" + source.url + (source.error ? "!" : "");
            for (const RepoPayload &p : source.payloads)
                signature += "," + p.key() + ":" + p.version;
        }
        if (signature != signature_)
        {
            signature_ = signature;
            rebuild(view);
        }
        if (ui::FormRow *row = form_.row(kRefreshRow))
            row->text = view.repository_busy ? "Fetching" : "";
        form_.set_disabled(kRefreshRow, view.repository_busy);
        for (const Entry &entry : entries_)
        {
            ui::FormRow *row = form_.row(entry.id);
            if (row == nullptr)
                continue;
            const bool busy = view.busy.count(busy_key(entry)) != 0;
            const RepoStatus status =
                hui::payloader::repo_status(entry.payload, entry.source_url, view.payloads);
            row->text = busy ? std::string("Installing") : status_text(entry.payload, status);
            form_.set_disabled(entry.id, busy);
        }
    }

    void activated(int id, app::Feedback &feedback) override
    {
        if (id == kRefreshRow)
        {
            hui::payloader::store().refresh_repository();
            return;
        }
        const Entry *entry = find(id);
        if (entry == nullptr)
            return;
        const RepoStatus status =
            hui::payloader::repo_status(entry->payload, entry->source_url, view().payloads);
        if (status.kind == RepoStatus::Kind::installed)
        {
            pending_ = entry->id;
            const std::string name = display_name(entry->payload);
            ask(kConfirmReinstall,
                {ui::StatusKind::info, name + " is installed",
                 status.self ? "This is pldmgr itself, and it is up to date."
                             : "Download and install this version again?",
                 status.self
                     ? std::vector<ui::DialogButton>{{"OK"}}
                     : std::vector<ui::DialogButton>{{"Cancel"},
                                                     {"Install again", ui::ButtonKind::primary}},
                 0},
                feedback);
            return;
        }
        install(*entry);
    }

    void west(int /*id*/, app::Feedback & /*feedback*/) override
    {
        hui::payloader::store().refresh_repository();
    }

    void dialog_chosen(int purpose, int choice, app::Feedback & /*feedback*/) override
    {
        if (purpose != kConfirmReinstall || choice != 1)
            return;
        if (const Entry *entry = find(pending_))
            install(*entry);
    }

    void draw_side(ui::Painter &paint) const override
    {
        const ui::Theme &look = paint.theme();
        const Rect inside = side_panel(paint, kSide);
        const float x = inside.x + 44.0f;
        const float width = inside.w - 88.0f;
        float y = inside.y + 64.0f;
        const Entry *entry = find(form_.focus_id());
        const View &now = view();
        if (entry == nullptr)
        {
            paint.label("REPOSITORIES", x, y, 19.0f, look.text_muted);
            std::string text =
                "Payloads come from pldmgr's repositories: its official one, and the ones you "
                "add on the Settings screen.";
            if (!now.config.multi_sources)
                text += " Extra repositories show here once \"Use extra repositories\" is on.";
            if (!now.repository_error.empty())
                text += " Last refresh: " + now.repository_error + ".";
            paragraph(paint, text, x, y + 50.0f, 24.0f, width, look.text);
            return;
        }
        const RepoPayload &p = entry->payload;
        const RepoStatus status = hui::payloader::repo_status(p, entry->source_url, now.payloads);
        paint.label(p.category.empty() ? "PAYLOAD" : p.category, x, y, 19.0f, look.text_muted);
        y += 62.0f;
        paint.heading(display_name(p), x, y, 40.0f, look.text);
        y += 50.0f;
        std::string line = p.version.empty() ? std::string() : "Version " + p.version;
        if (!p.author.empty())
            line += (line.empty() ? "By " : ", by ") + p.author;
        if (!line.empty())
        {
            paint.body(line, x, y, 23.0f, look.text_muted);
            y += 44.0f;
        }
        if (status.installed != nullptr)
        {
            const std::string have = status.installed->version.empty()
                                         ? std::string("Installed")
                                         : "Installed: " + status.installed->version;
            paint.body(have, x, y, 23.0f,
                       status.kind == RepoStatus::Kind::update ? look.primary : look.accent);
            y += 44.0f;
        }
        else if (status.conflict != nullptr)
        {
            paint.body("Installed from " + status.conflict->source_name, x, y, 23.0f, look.primary);
            y += 44.0f;
        }
        if (!p.description.empty())
            y = paragraph(paint, p.description, x, y, 23.0f, width, look.text, 5) + 10.0f;
        if (!p.release_notes.empty())
        {
            paint.label("WHAT IS NEW", x, y + 10.0f, 17.0f, look.text_muted);
            paragraph(paint, p.release_notes, x, y + 44.0f, 21.0f, width, look.text, 5, 1.35f);
        }
    }

  private:
    struct Entry
    {
        int id = 0;
        std::string source_url;
        RepoPayload payload;
    };

    static std::string display_name(const RepoPayload &p)
    {
        return !p.name.empty() ? p.name : p.filename;
    }

    static std::string busy_key(const Entry &entry)
    {
        return "install:" + entry.source_url + "#" + entry.payload.key();
    }

    static std::string status_text(const RepoPayload &p, const RepoStatus &status)
    {
        switch (status.kind)
        {
        case RepoStatus::Kind::update:
            return "Update " + p.version;
        case RepoStatus::Kind::installed:
            return "Installed";
        case RepoStatus::Kind::not_installed:
            break;
        }
        return p.version;
    }

    void rebuild(const View &view)
    {
        begin_rebuild();
        entries_.clear();
        form_.add_action(kRefreshRow, "Refresh all repositories").description =
            "pldmgr fetches every repository again (Square does the same)";
        if (!view.have_repository)
        {
            form_.add_value(kLoadingRow, view.repository_busy ? "Loading the repositories" : "-",
                            "");
        }
        for (const RepoSource &source : view.repository)
        {
            std::string header = source.name.empty() ? source.url : source.name;
            if (source.error)
                header += " (not reachable)";
            form_.add_header(header);
            for (const RepoPayload &p : source.payloads)
            {
                if (!hui::payloader::repo_payload_visible(p, source.url))
                    continue;
                Entry entry;
                entry.id = ids_.id(source.url + "#" + p.key());
                entry.source_url = source.url;
                entry.payload = p;
                ui::FormRow &row = form_.add_action(entry.id, display_name(p));
                row.description = first_line(p.description);
                entries_.push_back(std::move(entry));
            }
        }
        end_rebuild();
    }

    const Entry *find(int id) const
    {
        for (const Entry &entry : entries_)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    void install(const Entry &entry)
    {
        hui::payloader::store().install(entry.payload, entry.source_url, InstallMode::normal, "");
    }

    RowIds ids_;
    std::vector<Entry> entries_;
    std::string signature_ = "?";
    int pending_ = 0;
};

} // namespace

std::unique_ptr<app::Concept> make_repository(app::Context &context)
{
    return std::make_unique<RepositoryScreen>(context);
}

} // namespace hui::concepts
