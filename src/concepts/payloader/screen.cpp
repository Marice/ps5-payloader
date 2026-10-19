// PayLoader - The frame every screen is built in: the store, a form, toasts, a menu, a dialog.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/payloader/screen.hpp"

#include "core/tween.hpp"

#include <utility>

namespace hui::concepts::payloader
{

namespace
{

// Dialog purposes of the base; screens use positive numbers.
constexpr int kConflict = -1;
constexpr int kSuiteStartup = -2; // "Also start the suite with the console?"

} // namespace

using hui::payloader::Link;
using hui::payloader::View;

Screen::Screen(app::Context &context) : context_(context)
{
    const ui::Theme &look = theme();
    toasts_.style.theme = look;
    toasts_.style.anchor = ui::ToastAnchor::bottom_right;
    toasts_.style.margin = 120.0f;
    menu_.style.theme = look;
    menu_.style.close_on_check = true; // a checkable item is an action here too
    dialog_.style.theme = look;
    dialog_.style.frosted = false;
    prompt_.style.theme = look;
    prompt_.style.frosted = false;
    prompt_.keyboard.style.bindings = ui::KeyboardBindings::standard();
}

const View &Screen::view() const
{
    return hui::payloader::store().view();
}

void Screen::enter()
{
    age_ = 0.0f;
    form_.enter();
}

void Screen::restyle()
{
    if (styled_ && reduced_ == context_.settings.reduced_motion)
        return;
    styled_ = true;
    reduced_ = context_.settings.reduced_motion;
    ui::FormStyle style;
    style.theme = theme();
    style.reduced_motion = reduced_;
    style.label_ratio = 0.5f;
    style.compact = true;
    form_.style = style;
    form_.set_bounds(kForm);
    toasts_.style.reduced_motion = reduced_;
    menu_.style.reduced_motion = reduced_;
    dialog_.style.reduced_motion = reduced_;
    prompt_.style.reduced_motion = reduced_;
}

void Screen::update(const InputFrame &input, float dt, app::Feedback &feedback)
{
    clock_ += dt;
    age_ += dt;
    hui::payloader::Store &store = hui::payloader::store();
    for (const hui::payloader::Topic topic : topics())
        store.want(topic);
    store.update(dt);
    restyle();
    pump_notices(feedback);
    const View &now = store.view();
    const bool linked = usable(now);
    show(now);

    if (dialog_.is_open())
    {
        const ui::Event event = dialog_.handle(input, feedback);
        if (event == ui::Event::activated)
        {
            if (dialog_purpose_ == kConflict && !conflicts_.empty())
            {
                const hui::payloader::Notice conflict = conflicts_.front();
                conflicts_.erase(conflicts_.begin());
                if (dialog_.choice() == 1)
                    store.install(conflict.payload, conflict.source_url,
                                  hui::payloader::InstallMode::replace, conflict.conflict_slot);
            }
            else if (dialog_purpose_ == kSuiteStartup)
            {
                if (dialog_.choice() == 1)
                    store.add_suite_to_chain();
            }
            else
            {
                dialog_chosen(dialog_purpose_, dialog_.choice(), feedback);
            }
        }
        else if (event == ui::Event::cancelled && dialog_purpose_ == kConflict &&
                 !conflicts_.empty())
        {
            conflicts_.erase(conflicts_.begin());
        }
    }
    else if (menu_.is_open())
    {
        const ui::Event event = menu_.handle(input, feedback);
        if ((event == ui::Event::activated || event == ui::Event::changed) && menu_.focus() >= 0)
            menu_chosen(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
    }
    else if (prompt_.is_open())
    {
        if (prompt_.handle(input, feedback) == ui::Event::activated)
            prompt_done(prompt_purpose_, prompt_.text(), feedback);
    }
    else if (!linked)
    {
        if (input.is_pressed(Action::confirm))
        {
            store.refresh();
            feedback.play(audio::Cue::select);
        }
    }
    else
    {
        const ui::Event event = form_.handle(input, feedback);
        if (event == ui::Event::activated)
            activated(form_.changed_id(), feedback);
        else if (event == ui::Event::changed)
            changed(form_.changed_id());
        else if (input.is_pressed(Action::north) && form_.focus_id() != 0)
            north(form_.focus_id(), feedback);
        else if (input.is_pressed(Action::west))
            west(form_.focus_id(), feedback);
    }
    if (!dialog_.is_open() && suite_question_)
    {
        suite_question_ = false;
        dialog_purpose_ = kSuiteStartup;
        dialog_.open(
            {ui::StatusKind::info,
             "Also start the suite with the console?",
             "ps5-exporter, ps5-homeassistant and ps5-signal then start after the exploit, "
             "in this order, a few seconds apart. Payloads that are in the list already keep "
             "their place.",
             {{"Not now"}, {"Add to start-up", ui::ButtonKind::primary}},
             1},
            feedback);
    }
    if (!dialog_.is_open() && !conflicts_.empty())
    {
        const hui::payloader::Notice &next = conflicts_.front();
        std::string body = "It is installed from " + (next.conflict_source.empty()
                                                          ? std::string("another repository")
                                                          : next.conflict_source);
        if (!next.conflict_version.empty())
            body += " (version " + next.conflict_version + ")";
        body += ". Replace that one with this repository's?";
        dialog_purpose_ = kConflict;
        dialog_.open({ui::StatusKind::warning, next.title, body, {{"Keep it"}, {"Replace"}}, 0},
                     feedback);
    }
    form_.set_active(linked && !dialog_.is_open() && !menu_.is_open() && !prompt_.is_open());
    form_.update(dt);
    menu_.update(dt);
    dialog_.update(dt);
    prompt_.update(dt);
    toasts_.update(dt, feedback);
}

void Screen::pump_notices(app::Feedback &feedback)
{
    (void)feedback;
    for (hui::payloader::Notice &notice : hui::payloader::store().take_notices())
    {
        if (notice.conflict)
        {
            conflicts_.push_back(std::move(notice));
            continue;
        }
        if (notice.suite_installed)
            suite_question_ = true;
        toasts_.push(notice.failed ? ui::StatusKind::warning : ui::StatusKind::success,
                     std::move(notice.title), std::move(notice.detail));
    }
}

void Screen::draw(app::Frame &frame) const
{
    const ui::Theme &look = theme();
    begin_frame(frame, look);
    ui::Canvas canvas{frame.scene, context_.fonts, 0, clock_};
    ui::Painter paint(frame.scene, context_.fonts, look);
    frame.scene.push_opacity(tween::cubic_out(age_ / 0.3f));
    const View &now = view();
    draw_header(paint, title(), summary(), now, clock_);
    if (usable(now))
    {
        form_.draw(canvas);
        draw_side(paint);
        draw_hints(frame.scene, context_.fonts, look, hints());
    }
    else
    {
        draw_link_card(paint, {kMargin, kTop, kRight - kMargin, 560.0f}, now);
        static constexpr ui::Hint kRetry[] = {{ui::Button::cross, "Check now"}};
        draw_hints(frame.scene, context_.fonts, look, kRetry);
    }
    frame.scene.pop_opacity();
    ui::Canvas above{frame.overlay, context_.fonts, 0, clock_};
    menu_.draw(above);
    prompt_.draw(above);
    dialog_.draw(above);
    toasts_.draw(above);
}

void Screen::toast(ui::StatusKind kind, std::string title, std::string detail)
{
    toasts_.push(kind, std::move(title), std::move(detail));
}

void Screen::open_menu(std::vector<ui::MenuItem> items, app::Feedback &feedback)
{
    menu_.set_items(std::move(items));
    menu_.set_bounds({0.0f, kTop - 40.0f, gfx::kVirtualWidth, kBottom - kTop + 80.0f});
    gfx::Rect anchor = form_.row_rect(form_.focus());
    anchor.x += anchor.w * 0.55f;
    anchor.w *= 0.45f;
    menu_.open(anchor, feedback);
}

void Screen::ask(int purpose, ui::DialogContent content, app::Feedback &feedback)
{
    dialog_purpose_ = purpose;
    dialog_.open(std::move(content), feedback);
}

void Screen::prompt(int purpose, const char *title, const char *placeholder, int max_length,
                    app::Feedback &feedback)
{
    prompt_purpose_ = purpose;
    prompt_.style.max_length = max_length;
    prompt_.style.auto_capital = false;
    prompt_.style.allow_empty = false;
    prompt_.set_title(title);
    prompt_.field.set_placeholder(placeholder);
    prompt_.open(feedback, "");
}

void Screen::begin_rebuild()
{
    kept_focus_ = form_.focus_id();
    form_.clear();
}

void Screen::end_rebuild()
{
    if (kept_focus_ != 0 && form_.row(kept_focus_) != nullptr)
        form_.focus_row(kept_focus_);
    form_.set_bounds(kForm);
}

} // namespace hui::concepts::payloader
