#include "toolbar.h"
#include <2d/ZCLabel.h>

namespace zonix_gui {

using control_style::CONTROL_HEIGHT;

Toolbar* Toolbar::create(zocos::Director& director, float width) {
    if (width <= 2.f * INSET)
        return nullptr;
    auto* label = control_style::make_label(director, "");
    if (!label)
        return nullptr;
    auto* toolbar = new (mstd::nothrow) Toolbar(director);
    if (!toolbar || !toolbar->init() || !toolbar->palette_.init()) {
        delete toolbar;
        return nullptr;
    }

    toolbar->autorelease();
    toolbar->setAnchorPoint({0.f, 0.f});
    toolbar->setContentSize({width, HEIGHT});
    toolbar->text_ = label;
    toolbar->addChild(label);
    toolbar->layout_text();
    return toolbar;
}

Button* Toolbar::add_button(const mstd::string& text, Button::Action action) {
    return place(Button::create(director_, text, mstd::move(action)));
}

Button* Toolbar::add_button(control_style::Glyph glyph, Button::Action action) {
    return place(Button::create(director_, glyph, mstd::move(action)));
}

Button* Toolbar::place(Button* button) {
    if (!button)
        return nullptr;
    const auto size = button->getContentSize();
    if (next_button_x_ + size.width > getContentSize().width - INSET)
        return nullptr;

    button->setPosition({next_button_x_, (HEIGHT - size.height) / 2.f});
    next_button_x_ += size.width + GAP;
    addChild(button);
    layout_text();
    return button;
}

void Toolbar::set_text(const mstd::string& text) {
    text_->setString(text);
    layout_text();
}

zocos::Rect Toolbar::field() const {
    float x = next_button_x_ > INSET ? next_button_x_ + GAP : INSET;
    return {x, (HEIGHT - CONTROL_HEIGHT) / 2.f, getContentSize().width - INSET - x, CONTROL_HEIGHT};
}

void Toolbar::layout_text() {
    const auto area = field();
    float available = area.width - 2.f * FIELD_INSET;
    bool fits = available > 0.f && text_->updateLayout();
    text_->setVisible(fits);
    if (fits)
        control_style::place_label(*text_, area.x + FIELD_INSET, HEIGHT / 2.f, 0.f, available);
}

void Toolbar::draw(zocos::Renderer& renderer, const zocos::Mat4& world) {
    using control_style::Color;
    const auto size = getContentSize();
    palette_.box(renderer, world, {0.f, 0.f, size.width, size.height}, Color::Panel);
    palette_.box(renderer, world, {0.f, 0.f, size.width, 1.f}, Color::Divider);
    if (text_->isVisible())
        palette_.frame(renderer, world, field(), control_style::CONTROL_RADIUS, Color::Field, Color::Divider);
}

}  // namespace zonix_gui
