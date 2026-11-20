#pragma once

#include "chrome.h"
#include "client.h"

namespace desktop {

using desktop_chrome::TITLE_HEIGHT;
using desktop_ipc::MAX_WINDOWS;

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
// The square at a resizable window's bottom-right corner that drags its size.
inline constexpr int RESIZE_GRIP = 14;
// Width of the outline drawn while a window is being resized.
inline constexpr int RESIZE_OUTLINE = 2;
inline constexpr int NO_WINDOW = -1;

// One slot of the window pool. A slot is free while its client is not running.
struct Window {
    Rect rect{};
    desktop_ipc::Client client;
    // The program running in the window. Meaningful while it is open.
    int app = desktop_ipc::NO_APP;
    // The file an editor window was opened for.
    desktop_ipc::EditPath path{};
    // How many windows of the same program were open when this one started.
    // Later windows of a program are placed further down and to the right.
    int step{};
    // The client was asked to close and has had no input since.
    bool close_requested{};
    // The window is hidden until its program is shown again from the Dock.
    bool minimized{};

    bool open() const { return client.alive(); }
    // Open and shown on the screen.
    bool visible() const { return open() && !minimized; }
    const desktop_ipc::AppSpec& spec() const { return desktop_ipc::APPS[app]; }
    desktop_ipc::Protocol protocol() const { return spec().protocol; }
};

using Windows = sys::array<Window, MAX_WINDOWS>;
// Window slots from front to back.
using Order = sys::array<int, MAX_WINDOWS>;
// The surface of each slot, used while a pixel client runs in it.
using Surfaces = sys::array<desktop_ipc::PixelSurface, MAX_WINDOWS>;

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

inline bool in_resize_grip(const Rect& window, Point point) {
    return point.x >= window.origin.x + window.width - RESIZE_GRIP && point.x < window.origin.x + window.width &&
           point.y >= window.origin.y + window.height - RESIZE_GRIP && point.y < window.origin.y + window.height;
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

// The front visible window, which has the keyboard.
inline int top_window(const Windows& windows, const Order& order) {
    for (int index : order)
        if (windows[index].visible())
            return index;
    return NO_WINDOW;
}

}  // namespace desktop
