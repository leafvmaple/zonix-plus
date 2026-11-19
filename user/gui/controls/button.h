#pragma once

#include "style.h"
#include <ui/UIWidget.h>

namespace zonix_gui {

class Button final : public zocos::ui::Widget {
public:
    using Action = mstd::function<void()>;
    enum class Alignment { Center, Left };
    // Flat buttons show a background only while pressed or selected.
    enum class Style { Raised, Flat, Accent, Danger };

    static Button* create(zocos::Director& director, const mstd::string& text, Action action);
    static Button* create(zocos::Director& director, control_style::Glyph glyph, Action action);
    void layout(zocos::Size size, Alignment alignment = Alignment::Center);
    void set_style(Style style) { style_ = style; }
    void set_enabled(bool enabled);
    void set_selected(bool selected) { selected_ = selected; }
    void set_render_layer(unsigned short layer);

private:
    explicit Button(zocos::Director& director) : palette_(director) {}
    static Button* make(zocos::Director& director, Action action);
    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override;
    bool hitTest(float x, float y) const override;

    control_style::Palette palette_;
    // Null for a glyph button.
    zocos::Label* label_{};
    control_style::Glyph glyph_{};
    Style style_ = Style::Raised;
    unsigned short render_layer_{};
    bool enabled_ = true;
    bool selected_{};
};

}  // namespace zonix_gui
