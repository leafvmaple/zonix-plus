#pragma once

#include "button.h"

namespace zonix_gui {

// A row of buttons followed by a single-line text field that fills the rest.
class Toolbar final : public zocos::Node {
public:
    static constexpr float HEIGHT = 40.f;

    static Toolbar* create(zocos::Director& director, float width);
    Button* add_button(const mstd::string& text, Button::Action action);
    Button* add_button(control_style::Glyph glyph, Button::Action action);
    void set_text(const mstd::string& text);
    // The text field stretches with the toolbar.
    void set_width(float width);

private:
    explicit Toolbar(zocos::Director& director) : director_(director), palette_(director) {}
    Button* place(Button* button);
    zocos::Rect field() const;
    void layout_text();
    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override;

    static constexpr float INSET = 8.f;
    static constexpr float GAP = 4.f;
    static constexpr float FIELD_INSET = 10.f;
    zocos::Director& director_;
    control_style::Palette palette_;
    zocos::Label* text_{};
    float next_button_x_ = INSET;
};

}  // namespace zonix_gui
