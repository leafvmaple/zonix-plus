#include "button.h"
#include <2d/ZCLabel.h>

namespace zonix_gui {

using control_style::CONTROL_HEIGHT;
using control_style::TEXT_INSET;

Button* Button::make(zocos::Director& director, Action action) {
    auto* button = new (mstd::nothrow) Button(director);
    if (!button || !button->init() || !button->palette_.init()) {
        delete button;
        return nullptr;
    }
    button->autorelease();
    button->setAnchorPoint({0.f, 0.f});
    button->addEventListener([action = mstd::move(action)](zocos::ui::Widget&) { action(); });
    return button;
}

Button* Button::create(zocos::Director& director, const mstd::string& text, Action action) {
    auto* label = control_style::make_label(director, text);
    if (!label || !label->updateLayout())
        return nullptr;
    auto* button = make(director, mstd::move(action));
    if (!button)
        return nullptr;

    float width = mstd::max(CONTROL_HEIGHT, label->getContentSize().width + 2.f * TEXT_INSET);
    button->label_ = label;
    button->addChild(label);
    button->layout({width, CONTROL_HEIGHT});
    return button;
}

Button* Button::create(zocos::Director& director, control_style::Glyph glyph, Action action) {
    auto* button = make(director, mstd::move(action));
    if (button) {
        button->glyph_ = glyph;
        button->layout({CONTROL_HEIGHT, CONTROL_HEIGHT});
    }
    return button;
}

void Button::layout(zocos::Size size, Alignment alignment) {
    setContentSize(size);
    if (!label_)
        return;
    const bool left = alignment == Alignment::Left;
    float available = mstd::max(1.f, size.width - 2.f * TEXT_INSET);
    control_style::place_label(*label_, left ? TEXT_INSET : size.width / 2.f, size.height / 2.f, left ? 0.f : 0.5f,
                               available);
}

void Button::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (label_)
        label_->setOpacity(enabled ? 1.f : control_style::DISABLED_OPACITY);
}

void Button::set_render_layer(unsigned short layer) {
    render_layer_ = layer;
    if (label_)
        label_->setRenderLayer(layer);
}

void Button::draw(zocos::Renderer& renderer, const zocos::Mat4& world) {
    using control_style::Color;
    using control_style::CONTROL_RADIUS;
    const auto order = zocos::makeRenderSortKey(0, render_layer_, 0);
    const auto size = getContentSize();
    const zocos::Rect area{0.f, 0.f, size.width, size.height};
    const bool pressed = enabled_ && isPressed();

    switch (style_) {
        case Style::Raised: {
            auto fill = Color::Disabled;
            if (enabled_)
                fill = pressed ? Color::Pressed : Color::Control;
            palette_.frame(renderer, world, area, CONTROL_RADIUS, fill, enabled_ ? Color::Border : Color::Divider,
                           order);
            break;
        }
        case Style::Flat:
            if (pressed || selected_)
                palette_.round_box(renderer, world, area, CONTROL_RADIUS, pressed ? Color::Pressed : Color::Selected,
                                   order);
            break;
        case Style::Accent:
        case Style::Danger: {
            const bool danger = style_ == Style::Danger;
            auto fill = Color::Disabled;
            if (enabled_ && pressed)
                fill = danger ? Color::DangerPressed : Color::AccentPressed;
            else if (enabled_)
                fill = danger ? Color::Danger : Color::Accent;
            palette_.round_box(renderer, world, area, CONTROL_RADIUS, fill, order);
            break;
        }
    }
    if (!label_)
        palette_.glyph(renderer, world, area, glyph_, enabled_ ? Color::Text : Color::TextDisabled, order);
}

bool Button::hitTest(float x, float y) const {
    for (const auto* node = static_cast<const zocos::Node*>(this); node; node = node->getParent()) {
        if (!node->isVisible())
            return false;
    }
    return enabled_ && Widget::hitTest(x, y);
}

}  // namespace zonix_gui
