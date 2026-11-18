#pragma once

#include "style.h"
#include <ui/UIWidget.h>

namespace zonix_gui {

class Button final : public zocos::ui::Widget {
public:
    using Action = mstd::function<void()>;
    enum class Alignment { Center, Left };

    static Button* create(zocos::Director& director, const mstd::string& text, Action action);
    void layout(zocos::Size size, Alignment alignment = Alignment::Center);
    void set_enabled(bool enabled);

private:
    explicit Button(zocos::Director& director) : palette_(director) {}
    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override;
    bool hitTest(float x, float y) const override;

    static constexpr float TEXT_INSET = 11.f;
    control_style::Palette palette_;
    zocos::Label* label_{};
    bool enabled_ = true;
};

}  // namespace zonix_gui
