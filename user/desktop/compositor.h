#pragma once

#include "desktop_icons.h"
#include "dock.h"

namespace desktop {

// Composes areas of the screen from the window stack: background, shadows,
// borders with rounded corners, title bars, client content and the pointer.
// What lies behind every window is kept as a desk layer, so an area is the
// desk copied row by row with the windows painted over it from back to front.
class Compositor {
public:
    // State borrowed from the window manager, which outlives the compositor.
    struct Scene {
        const Windows& windows;
        const Order& order;
        const Surfaces& surfaces;
        const desktop_chrome::Chrome& chrome;
        const Point& pointer;
        Rect screen;
        // Empty when the screen has no room for the Dock.
        Rect dock;
        // The column of desktop icons, empty when there are none.
        Rect icons;
        // The height of the menu bar, or 0 on a screen without one.
        int menu_height;
        // The new size of a window being resized, or empty.
        const Rect& resize_outline;
    };

    explicit Compositor(const Scene& scene) : scene_(scene) {}

    // Allocates the desk layer. Returns 0 or a negative error.
    long init();
    // Redraws the desk layer in an area whose background, menu bar, icons or
    // Dock changed.
    void refresh_desk(const Rect& area);
    // Composes an area inside the screen into `out`, which holds the area's
    // top-left pixel and has rows `stride` pixels apart.
    void render(const Rect& area, unsigned int* out, int stride) const;
    Rect cursor_rect() const;
    // Draws the pointer into a DISPLAY_CURSOR_SIZE square ARGB image whose
    // top-left pixel is the hot spot, for the display's cursor plane.
    static void cursor_image(unsigned int* out);
    // With a cursor plane showing the pointer, areas are composed without it.
    void set_hardware_cursor(bool hardware) { hardware_cursor_ = hardware; }
    // The area a window paints, including its shadow.
    static Rect bounds(const Rect& window);
    // The part of `area` inside `clip`, empty when they do not meet.
    static Rect intersect(const Rect& area, const Rect& clip);

private:
    struct FreePixels {
        void operator()(unsigned int* pixels) const noexcept;
    };

    void paint_shadow(const Rect& window, bool focused, const Rect& area, unsigned int* out, int stride) const;
    void paint_window(int index, bool focused, const Rect& area, unsigned int* out, int stride) const;
    unsigned int window_pixel(int index, bool focused, Point point, unsigned int below) const;
    unsigned int interior(int index, Point point, bool focused) const;
    unsigned int desk(Point point, unsigned int wallpaper) const;

    Scene scene_;
    // The screen without windows or pointer, row after row.
    sys::unique_ptr<unsigned int, FreePixels> desk_;
    bool hardware_cursor_{};
};

}  // namespace desktop
