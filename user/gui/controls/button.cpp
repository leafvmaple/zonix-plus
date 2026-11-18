#include "button.h"
#include <2d/ZCLabel.h>

namespace zonix_gui {

Button* Button::create(zocos::Director& director, const mstd::string& text, Action action) {
    auto* label = control_style::make_label(director, text);
    if (!label || !label->updateLayout())
        return nullptr;
    auto* button = new (mstd::nothrow) Button(director);
    if (!button || !button->init() || !button->palette_.init()) {
        delete button;
        return nullptr;
    }

    constexpr float height = 29.f;
    float width = mstd::max(32.f, label->getContentSize().width * control_style::LABEL_SCALE + 2.f * TEXT_INSET);
    button->autorelease();
    button->setAnchorPoint({0.f, 0.f});
    button->label_ = label;
    button->layout({width, height});
    button->addChild(label);
    button->addEventListener([action = mstd::move(action)](zocos::ui::Widget&) { action(); });
    return button;
}

void Button::layout(zocos::Size size, Alignment alignment) {
    setContentSize(size);
    const bool left = alignment == Alignment::Left;
    label_->setAnchorPoint({left ? 0.f : 0.5f, 0.5f});
    label_->setPosition({left ? TEXT_INSET : size.width / 2.f, size.height / 2.f});
    float available = mstd::max(0.f, size.width - 2.f * TEXT_INSET);
    float measured = mstd::max(label_->getContentSize().width, 1.f);
    label_->setScale(mstd::min(control_style::LABEL_SCALE, available / measured));
}

void Button::set_enabled(bool enabled) {
    enabled_ = enabled;
    label_->setOpacity(enabled ? 1.f : 0.45f);
}

void Button::draw(zocos::Renderer& renderer, const zocos::Mat4& world) {
    using control_style::Color;
    auto color = Color::Disabled;
    if (enabled_)
        color = isPressed() ? Color::Pressed : Color::Button;
    palette_.button(renderer, world, getContentSize(), color);
}

bool Button::hitTest(float x, float y) const {
    for (const auto* node = static_cast<const zocos::Node*>(this); node; node = node->getParent()) {
        if (!node->isVisible())
            return false;
    }
    return enabled_ && Widget::hitTest(x, y);
}

}  // namespace zonix_gui
