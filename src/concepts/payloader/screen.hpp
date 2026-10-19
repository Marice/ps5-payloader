// PayLoader - The frame every screen is built in: the store, a form, toasts, a menu, a dialog.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A screen names the topics it shows, fills its form from the store's view,
// and answers what the player chose. This base routes the input (dialog,
// menu, keyboard, form, in that order), shows the store's notices as toasts,
// asks about install conflicts wherever they arrive, and draws the "no
// pldmgr" card in place of the form while pldmgr is away.

#pragma once

#include "concepts/payloader/look.hpp"
#include "ui/components/dialog.hpp"
#include "ui/components/form.hpp"
#include "ui/components/input_prompt.hpp"
#include "ui/components/menu.hpp"
#include "ui/components/toast.hpp"

#include <span>
#include <string>
#include <vector>

namespace hui::concepts::payloader
{

// A menu item with a label, an optional quiet text at its end, and a tag.
inline ui::MenuItem menu_item(std::string label, int tag, std::string shortcut = {})
{
    ui::MenuItem item;
    item.label = std::move(label);
    item.shortcut = std::move(shortcut);
    item.tag = tag;
    return item;
}

class Screen : public app::Concept
{
  public:
    explicit Screen(app::Context &context);

    void enter() override;
    void update(const InputFrame &input, float dt, app::Feedback &feedback) override;
    void draw(app::Frame &frame) const override;

  protected:
    virtual const char *title() const = 0;
    virtual const char *summary() const = 0;
    virtual std::span<const hui::payloader::Topic> topics() const = 0;
    virtual std::span<const ui::Hint> hints() const = 0;
    // Called each frame with the latest view; set rows' texts here, rebuild when the list changed.
    virtual void show(const hui::payloader::View &view) = 0;
    virtual void activated(int id, app::Feedback &feedback) = 0;
    virtual void changed(int /*id*/)
    {
    }
    // Triangle and Square on the focused row.
    virtual void north(int /*id*/, app::Feedback & /*feedback*/)
    {
    }
    virtual void west(int /*id*/, app::Feedback & /*feedback*/)
    {
    }
    virtual void menu_chosen(int /*tag*/, app::Feedback & /*feedback*/)
    {
    }
    // A dialog this screen opened with ask(); choice is the button's index.
    virtual void dialog_chosen(int /*purpose*/, int /*choice*/, app::Feedback & /*feedback*/)
    {
    }
    virtual void prompt_done(int /*purpose*/, const std::string & /*text*/,
                             app::Feedback & /*feedback*/)
    {
    }
    virtual void draw_side(ui::Painter &paint) const = 0;
    // Whether the screen has something to show; otherwise the "no pldmgr" card takes its place.
    virtual bool usable(const hui::payloader::View &view) const
    {
        return view.link == hui::payloader::Link::ready;
    }

    const hui::payloader::View &view() const;
    void toast(ui::StatusKind kind, std::string title, std::string detail = {});
    // A context menu at the focused row.
    void open_menu(std::vector<ui::MenuItem> items, app::Feedback &feedback);
    void ask(int purpose, ui::DialogContent content, app::Feedback &feedback);
    void prompt(int purpose, const char *title, const char *placeholder, int max_length,
                app::Feedback &feedback);

    // Rebuilding the form keeps the focus on the same row id where it still exists.
    void begin_rebuild();
    void end_rebuild();

    app::Context &context_;
    ui::Form form_;
    float clock_ = 0.0f;
    static constexpr gfx::Rect kForm{kMargin, kTop, kSplit - kMargin - 50.0f, kBottom - kTop};
    static constexpr gfx::Rect kSide{kSplit, kTop + 10.0f, kRight - kSplit, 630.0f};

  private:
    void restyle();
    void pump_notices(app::Feedback &feedback);

    ui::ToastStack toasts_;
    ui::Menu menu_;
    ui::Dialog dialog_;
    ui::InputPrompt prompt_;
    int dialog_purpose_ = 0;
    int prompt_purpose_ = 0;
    int kept_focus_ = 0;
    bool suite_question_ = false;                   // the suite was installed: ask about start-up
    std::vector<hui::payloader::Notice> conflicts_; // waiting for the dialog
    float age_ = 0.0f;
    bool reduced_ = false;
    bool styled_ = false;
};

} // namespace hui::concepts::payloader
