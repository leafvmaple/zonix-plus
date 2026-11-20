#pragma once

#include "window.h"

// The bar at the bottom of the screen that starts programs. Its geometry is
// shared by Desktop, which handles clicks, the compositor, which draws the bar
// and the running marks, and the chrome, which draws the icons.
namespace desktop_dock {

using desktop::Point;
using desktop::Rect;

// An item is a program, or ITEM_EXIT to leave Desktop.
inline constexpr int ITEM_EXIT = -2;
inline constexpr int ITEMS[] = {desktop_ipc::APP_TERMINAL, desktop_ipc::APP_FILES, desktop_ipc::APP_EDITOR,
                                desktop_ipc::APP_SETTINGS, ITEM_EXIT};
inline constexpr int ITEM_COUNT = sizeof(ITEMS) / sizeof(*ITEMS);
inline constexpr int NO_ITEM = -1;

inline constexpr int ICON = 44;
inline constexpr int GAP = 10;
inline constexpr int PADDING = 10;
// Room under the icons for the mark of a running program.
inline constexpr int MARK_ROOM = 6;
inline constexpr int MARK_SIZE = 4;
inline constexpr int WIDTH = ITEM_COUNT * ICON + (ITEM_COUNT - 1) * GAP + 2 * PADDING;
inline constexpr int HEIGHT = ICON + 2 * PADDING + MARK_ROOM;
inline constexpr int MARGIN = 10;
inline constexpr int RADIUS = 14;

// The bar, centred at the bottom of the screen. A screen too small for the
// bar and a window above it has none.
inline constexpr Rect bounds(int screen_width, int screen_height) {
    if (screen_width < WIDTH + 2 * MARGIN || screen_height < 4 * HEIGHT)
        return {};
    return {{(screen_width - WIDTH) / 2, screen_height - MARGIN - HEIGHT}, WIDTH, HEIGHT};
}

// The top-left corner of an item's icon.
inline constexpr Point icon_origin(const Rect& dock, int item) {
    return {dock.origin.x + PADDING + item * (ICON + GAP), dock.origin.y + PADDING};
}

// The item whose icon is at a point, or NO_ITEM.
inline constexpr int item_at(const Rect& dock, Point point) {
    for (int item = 0; item < ITEM_COUNT; ++item) {
        Point origin = icon_origin(dock, item);
        if (point.x >= origin.x && point.x < origin.x + ICON && point.y >= origin.y && point.y < origin.y + ICON)
            return item;
    }
    return NO_ITEM;
}

// The mark under an item's icon.
inline constexpr Rect mark(const Rect& dock, int item) {
    Point origin = icon_origin(dock, item);
    return {{origin.x + (ICON - MARK_SIZE) / 2, origin.y + ICON + (PADDING + MARK_ROOM - MARK_SIZE) / 2}, MARK_SIZE,
            MARK_SIZE};
}

}  // namespace desktop_dock
