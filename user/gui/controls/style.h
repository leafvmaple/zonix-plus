#pragma once

#include <base/ZCRenderer.h>

namespace zocos {
class Director;
class Label;
}  // namespace zocos

namespace zonix_gui::control_style {

inline constexpr float LABEL_SCALE = 0.66f;

enum class Color {
    Border,
    Button,
    Pressed,
    Disabled,
    Toolbar,
    Sidebar,
    Selected,
    Folder,
    FolderShade,
    File,
    FileShade
};

zocos::Label* make_label(zocos::Director& director, const mstd::string& text);

class Palette {
public:
    explicit Palette(zocos::Director& director) : director_(director) {}
    ~Palette();
    Palette(const Palette&) = delete;
    Palette& operator=(const Palette&) = delete;

    bool init();
    void box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Color color,
             zocos::RenderSortKey order = 0) const;
    void button(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Size size, Color color,
                zocos::RenderSortKey order = 0) const;

private:
    zocos::Director& director_;
    zocos::TextureHandle texture_{};
};

}  // namespace zonix_gui::control_style
