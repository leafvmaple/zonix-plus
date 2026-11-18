#include "toolbar.h"
#include <2d/ZCLabel.h>

namespace zonix_gui {

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
    toolbar->setContentSize({width, 46.f});
    label->setAnchorPoint({0.f, 0.5f});
    toolbar->text_ = label;
    toolbar->addChild(label);
    toolbar->layout_text();
    return toolbar;
}

Button* Toolbar::add_button(const mstd::string& text, Button::Action action) {
    auto* button = Button::create(director_, text, mstd::move(action));
    if (!button)
        return nullptr;
    const auto size = button->getContentSize();
    if (next_button_x_ + size.width > getContentSize().width - INSET)
        return nullptr;

    constexpr float gap = 5.f;
    button->setPosition({next_button_x_, (getContentSize().height - size.height) / 2.f});
    next_button_x_ += size.width + gap;
    addChild(button);
    layout_text();
    return button;
}

void Toolbar::set_text(const mstd::string& text) {
    text_->setString(text);
    layout_text();
}

void Toolbar::layout_text() {
    float x = next_button_x_;
    if (x > INSET)
        x += 8.f;
    float available = getContentSize().width - INSET - x;
    bool fits = available > 0.f && text_->updateLayout();
    text_->setVisible(fits);
    if (fits) {
        text_->setPosition({x, getContentSize().height / 2.f});
        float measured = text_->getContentSize().width;
        text_->setScale(mstd::min(control_style::LABEL_SCALE, available / mstd::max(measured, 1.f)));
    }
}

void Toolbar::draw(zocos::Renderer& renderer, const zocos::Mat4& world) {
    using control_style::Color;
    const auto size = getContentSize();
    palette_.box(renderer, world, {0.f, 0.f, size.width, size.height}, Color::Toolbar);
    palette_.box(renderer, world, {0.f, 0.f, size.width, 1.f}, Color::Border);
}

}  // namespace zonix_gui
