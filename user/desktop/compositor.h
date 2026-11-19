#pragma once

#include "dock.h"
#include <kernel/psf.h>

namespace desktop {

// Produces the colour of any screen pixel from the window stack: background,
// shadows, borders with rounded corners, title bars, client content and the
// pointer. It draws nothing itself; Desktop presents the pixels it asks for.
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
        psf::Font font;
    };

    explicit Compositor(const Scene& scene) : scene_(scene) {}

    unsigned int color(Point point) const;
    Rect cursor_rect() const;
    // The area a window paints, including its shadow.
    static Rect bounds(const Rect& window);

private:
    unsigned int compose(Point point, int slot, int top) const;
    unsigned int interior(int index, Point point, bool focused) const;
    unsigned int desk(Point point) const;
    unsigned int background(Point point) const;
    unsigned int text_color(int index, int x, int y) const;

    Scene scene_;
};

}  // namespace desktop
