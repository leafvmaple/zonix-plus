#pragma once

#include "chrome.h"
#include "client.h"

namespace desktop {

using desktop_chrome::TITLE_HEIGHT;
using desktop_ipc::WINDOW_COUNT;

struct Point {
    int x;
    int y;
};

struct Rect {
    Point origin;
    int width;
    int height;

    bool contains(Point point) const {
        return point.x >= origin.x && point.x < static_cast<long>(origin.x) + width && point.y >= origin.y &&
               point.y < static_cast<long>(origin.y) + height;
    }
};

// Width of the border and gap between a window's outline and its client
// surface on the left, right and bottom. The title bar is above the surface.
inline constexpr int FRAME = 2;
inline constexpr int NO_WINDOW = -1;

struct Window {
    Rect rect{};
    desktop_ipc::Client client;
    bool open() const { return client.alive(); }
};

using Windows = sys::array<Window, WINDOW_COUNT>;
// Window indices from front to back.
using Order = sys::array<int, WINDOW_COUNT>;
using Surfaces = sys::array<desktop_ipc::PixelSurface, WINDOW_COUNT - desktop_ipc::FIRST_PIXEL_WINDOW>;

inline int clamp(long value, int limit) {
    if (value < 0)
        return 0;
    return value > limit ? limit : static_cast<int>(value);
}

// The outer size of a window around a client surface.
inline constexpr int outer_width(int surface_width) {
    return surface_width + 2 * FRAME;
}
inline constexpr int outer_height(int surface_height) {
    return surface_height + TITLE_HEIGHT + FRAME;
}

// The screen position of the top-left pixel of a window's client surface.
inline Point client_origin(const Rect& window) {
    return {window.origin.x + FRAME, window.origin.y + TITLE_HEIGHT};
}

inline bool in_body(const Rect& window, Point point) {
    return point.x >= window.origin.x + FRAME && point.x < window.origin.x + window.width - FRAME &&
           point.y >= window.origin.y + TITLE_HEIGHT && point.y < window.origin.y + window.height - FRAME;
}

// Maps a screen point to a cell of the scaled frame an example client draws.
inline Point surface_point(const Rect& window, Point point) {
    constexpr int size = desktop_ipc::SURFACE_SIZE;
    int width = window.width - 2 * FRAME;
    int height = window.height - TITLE_HEIGHT - FRAME;
    int x = clamp(static_cast<long>(point.x) - window.origin.x - FRAME, width - 1);
    int y = clamp(static_cast<long>(point.y) - window.origin.y - TITLE_HEIGHT, height - 1);
    return {static_cast<int>(static_cast<long>(x) * size / width),
            static_cast<int>(static_cast<long>(y) * size / height)};
}

inline int top_window(const Windows& windows, const Order& order) {
    for (int index : order)
        if (windows[index].open())
            return index;
    return NO_WINDOW;
}

}  // namespace desktop
