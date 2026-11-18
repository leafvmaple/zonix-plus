#pragma once

#include "button.h"

namespace zonix_gui {

class Toolbar final : public zocos::Node {
public:
    static Toolbar* create(zocos::Director& director, float width);
    Button* add_button(const mstd::string& text, Button::Action action);
    void set_text(const mstd::string& text);

private:
    explicit Toolbar(zocos::Director& director) : director_(director), palette_(director) {}
    void layout_text();
    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override;

    static constexpr float INSET = 10.f;
    zocos::Director& director_;
    control_style::Palette palette_;
    zocos::Label* text_{};
    float next_button_x_ = INSET;
};

}  // namespace zonix_gui
