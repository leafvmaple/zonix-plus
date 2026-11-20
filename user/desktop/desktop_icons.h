#pragma once

#include "window.h"

// The icons in a column at the top right of the desktop, below the menu bar.
// Each opens a folder in Files. Their geometry is shared by Desktop, which
// handles clicks, the compositor, which places them over the background, and
// the chrome, which draws them.
namespace desktop_icons {

using desktop::Point;
using desktop::Rect;

enum class Kind { Disk, Folder };

struct Item {
    const char* label;
    const char* path;
    Kind kind;
};

inline constexpr Item ITEMS[] = {
    {"Zonix", "/", Kind::Disk},
    {"Home", "/home", Kind::Folder},
    {"Applications", "/opt", Kind::Folder},
};
inline constexpr int ITEM_COUNT = sizeof(ITEMS) / sizeof(*ITEMS);
inline constexpr int NO_ITEM = -1;

// An item is a cell with the icon at the top and its label underneath.
inline constexpr int CELL_WIDTH = 104;
inline constexpr int CELL_HEIGHT = 84;
inline constexpr int ICON = 48;
inline constexpr int ICON_TOP = 4;
// The rounded backdrop behind a selected icon.
inline constexpr int BACKDROP = 58;
inline constexpr int BACKDROP_TOP = ICON_TOP - (BACKDROP - ICON) / 2;
inline constexpr int LABEL_CENTER = 67;
inline constexpr int LABEL_HEIGHT = 20;
inline constexpr int LABEL_PADDING = 6;
inline constexpr int MARGIN = 12;

// The column of cells: below the menu bar at the right edge, and above the
// Dock, whose top is `bottom`. A screen too small for the column has none.
inline constexpr Rect bounds(int screen_width, int top, int bottom) {
    int height = ITEM_COUNT * CELL_HEIGHT;
    if (ITEM_COUNT == 0 || screen_width < 4 * CELL_WIDTH || bottom - top < height + 2 * MARGIN)
        return {};
    return {{screen_width - MARGIN - CELL_WIDTH, top + MARGIN}, CELL_WIDTH, height};
}

inline constexpr Rect cell(const Rect& column, int item) {
    return {{column.origin.x, column.origin.y + item * CELL_HEIGHT}, CELL_WIDTH, CELL_HEIGHT};
}

// The item whose cell is at a point, or NO_ITEM.
inline int item_at(const Rect& column, Point point) {
    if (column.width == 0 || !column.contains(point))
        return NO_ITEM;
    return (point.y - column.origin.y) / CELL_HEIGHT;
}

}  // namespace desktop_icons
