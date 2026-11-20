#pragma once

#include <base/ZCView.h>
#include <platform/software/ZCSoftwareRenderDevice.h>

namespace zocos {
class Director;
}

namespace zonix_gui {

inline constexpr const char* UI_FONT_PATH = "/opt/fonts/UI.TTF";
inline constexpr float POINTER_RESET_COORDINATE = -1.f;

inline bool is_pointer_reset(zocos::Vec2 pointer) {
    return pointer.x == POINTER_RESET_COORDINATE && pointer.y == POINTER_RESET_COORDINATE;
}

bool is_double_click(unsigned int modifiers);

// How a window follows a resize from Desktop. The view clamps the proposed
// size to the minimum, resizes the engine's framebuffer and then calls
// `relayout` with the new size before the next frame is drawn.
struct Resize {
    int min_width{};
    int min_height{};
    mstd::function<void(int width, int height)> relayout;
};

// Either callback may be empty. A client that handles close requests exits by
// itself; Desktop closes every other window directly. Without a relayout
// function the window keeps its size.
mstd::unique_ptr<zocos::View> make_window_view(mstd::function<void(int)> on_edit_error,
                                               mstd::function<void()> on_close_request = {}, Resize resize = {});

// The caller owns the RGBA surface and keeps it alive until Director::shutdown.
bool init_offscreen(zocos::Director& director, const zocos::PixelBuffer& buffer, zocos::Color4B background);

// An opaque engine colour from an XRGB8888 theme value.
inline constexpr zocos::Color4B opaque_color(unsigned int xrgb) {
    return {static_cast<unsigned char>(xrgb >> 16), static_cast<unsigned char>(xrgb >> 8),
            static_cast<unsigned char>(xrgb), 255};
}

inline unsigned int xrgb(const unsigned char* rgba) {
    return (static_cast<unsigned int>(rgba[0]) << 16) | (static_cast<unsigned int>(rgba[1]) << 8) | rgba[2];
}

void finish();

}  // namespace zonix_gui
