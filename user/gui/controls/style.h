#pragma once

#include <base/ZCRenderer.h>
#include <sys/array.hpp>

namespace zocos {
class Director;
class Label;
}  // namespace zocos

namespace zonix_gui::control_style {

// Every label of a process is rasterised at one size and drawn unscaled, so
// glyph texels map to whole pixels and the process keeps one font atlas. A
// program sets the size from the settings before it creates its first label.
class TextSize {
public:
    static float get() { return value_; }
    static void set(int pixels) { value_ = static_cast<float>(pixels); }

private:
    inline static float value_ = 16.f;
};
inline constexpr float CONTROL_HEIGHT = 28.f;
inline constexpr float CONTROL_RADIUS = 6.f;
inline constexpr float PANEL_RADIUS = 8.f;
inline constexpr float TEXT_INSET = 12.f;
inline constexpr float GLYPH_SIZE = 12.f;
inline constexpr float SECONDARY_OPACITY = 0.62f;
inline constexpr float DISABLED_OPACITY = 0.38f;

enum class Color {
    Window,
    Panel,
    Sidebar,
    Border,
    Divider,
    Control,
    Pressed,
    Disabled,
    Field,
    Selected,
    SelectedBorder,
    Accent,
    AccentPressed,
    Danger,
    DangerPressed,
    Text,
    TextDisabled,
    Close,
    Folder,
    FolderShade,
    FolderTab,
    File,
    FileShade,
    FileFold,
    Shadow,
    Scrim
};

enum class Glyph { Close, Back, Forward, Up };

zocos::Label* make_label(zocos::Director& director, const mstd::string& text);

// Positions a single-line label on whole pixels. `x` is the left edge, centre
// or right edge for an `align` of 0, 0.5 or 1, and `center_y` is the middle of
// the line. A positive `max_width` shrinks text that would not fit.
void place_label(zocos::Label& label, float x, float center_y, float align, float max_width = 0.f);

class Palette {
public:
    explicit Palette(zocos::Director& director) : director_(director) {}
    ~Palette();
    Palette(const Palette&) = delete;
    Palette& operator=(const Palette&) = delete;

    bool init();
    // Areas use whole-pixel node coordinates with a bottom-left origin.
    void box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Color color,
             zocos::RenderSortKey order = 0) const;
    void round_box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, float radius, Color color,
                   zocos::RenderSortKey order = 0) const;
    // A rounded box with a one-pixel border.
    void frame(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, float radius, Color fill,
               Color border, zocos::RenderSortKey order = 0) const;
    void button(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Size size, Color color,
                zocos::RenderSortKey order = 0) const;
    // Draws a GLYPH_SIZE square symbol centred in the area.
    void glyph(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Glyph symbol, Color color,
               zocos::RenderSortKey order = 0) const;

private:
    static constexpr int MAX_RADIUS = 8;
    static constexpr int GLYPH_COUNT = static_cast<int>(Glyph::Up) + 1;

    // Handles default to invalid.
    struct Textures {
        zocos::TextureHandle solid;
        // discs[r - 1] is an antialiased disc of radius r in a 2r square.
        sys::array<zocos::TextureHandle, MAX_RADIUS> discs;
        sys::array<zocos::TextureHandle, GLYPH_COUNT> glyphs;
    };
    static bool create(zocos::RenderDevice& device);
    static void destroy(zocos::RenderDevice* device);

    // Every palette in the process draws with one set of textures, created by
    // the first and released with the last.
    inline static Textures textures_;
    inline static int users_{};

    zocos::Director& director_;
    bool attached_{};
};

}  // namespace zonix_gui::control_style
