#include "compositor.h"
#include "../gui/shape.h"
#include "../gui/theme.h"
#include <minilib.h>
#include <sys/algorithm.hpp>
#include <sys/cstdlib.hpp>

namespace desktop {

namespace {

using namespace desktop_ipc;
namespace theme = zonix_gui::theme;

// Coverage of the pixels in a rounded corner, indexed by their distance from
// the two edges that meet there.
template<int RADIUS>
struct CornerMask {
    unsigned char alpha[RADIUS][RADIUS];

    constexpr CornerMask() : alpha{} {
        for (int y = 0; y < RADIUS; ++y) {
            for (int x = 0; x < RADIUS; ++x)
                alpha[y][x] = zonix_gui::shape::disc_coverage(RADIUS, x, y);
        }
    }

    constexpr int at(int x, int y) const { return x < RADIUS && y < RADIUS ? alpha[y][x] : 255; }
};

constexpr int CORNER_RADIUS = 10;
constexpr CornerMask<CORNER_RADIUS> OUTER_CORNER{};
constexpr CornerMask<CORNER_RADIUS - 1> INNER_CORNER{};
constexpr CornerMask<desktop_dock::RADIUS> DOCK_CORNER{};
// The shadow is the window's outline moved down and faded over the blur distance.
constexpr int SHADOW_OFFSET = 8;
constexpr int SHADOW_BLUR = 28;
constexpr int SHADOW_MARGIN = SHADOW_BLUR + SHADOW_OFFSET;
constexpr int SHADOW_FOCUSED = 132;
constexpr int SHADOW_UNFOCUSED = 64;

// Shadow darkness out of 255 by the distance from the moved outline along
// each axis, for one strength.
struct ShadowTable {
    unsigned char shade[SHADOW_BLUR][SHADOW_BLUR];

    constexpr explicit ShadowTable(int strength) : shade{} {
        for (int dy = 0; dy < SHADOW_BLUR; ++dy) {
            for (int dx = 0; dx < SHADOW_BLUR; ++dx) {
                int square = dx * dx + dy * dy;
                int distance = 0;
                while ((distance + 1) * (distance + 1) <= square)
                    ++distance;
                int fade = distance < SHADOW_BLUR ? SHADOW_BLUR - distance : 0;
                shade[dy][dx] = static_cast<unsigned char>(strength * fade * fade / (SHADOW_BLUR * SHADOW_BLUR));
            }
        }
    }
};

constexpr ShadowTable FOCUSED_SHADOW{SHADOW_FOCUSED};
constexpr ShadowTable UNFOCUSED_SHADOW{SHADOW_UNFOCUSED};
// How far an unfocused title bar moves towards theme::TITLE_INACTIVE, out of 255.
constexpr int INACTIVE_TITLE = 120;
constexpr int CURSOR_WIDTH = 12;
constexpr int CURSOR_HEIGHT = 19;
// 'X' is the pointer's outline and '.' its fill. The hot spot is the top-left pixel.
constexpr const char* CURSOR[CURSOR_HEIGHT] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ",
    "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX", "X...X..X    ", "X..XX..X    ",
    "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ", "       XX   "};
constexpr unsigned char DITHER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Blends towards `to` by `amount` out of 255.
unsigned int mix(unsigned int from, unsigned int to, int amount) {
    unsigned int weight = static_cast<unsigned int>(amount + (amount >> 7));
    unsigned int red_blue = (((from & 0xFF00FF) * (256 - weight) + (to & 0xFF00FF) * weight) >> 8) & 0xFF00FF;
    unsigned int green = (((from & 0x00FF00) * (256 - weight) + (to & 0x00FF00) * weight) >> 8) & 0x00FF00;
    return red_blue | green;
}

// Shadow darkness out of 255 at a point outside the window or in its corner gaps.
int shadow(const Rect& window, Point point, bool focused) {
    int left = window.origin.x;
    int right = left + window.width - 1;
    int top = window.origin.y + SHADOW_OFFSET;
    int bottom = top + window.height - 1;
    int dx = point.x < left ? left - point.x : (point.x > right ? point.x - right : 0);
    int dy = point.y < top ? top - point.y : (point.y > bottom ? point.y - bottom : 0);
    if (dx >= SHADOW_BLUR || dy >= SHADOW_BLUR)
        return 0;
    return (focused ? FOCUSED_SHADOW : UNFOCUSED_SHADOW).shade[dy][dx];
}

// The pixel of `out` for a screen point, where `out` holds `area` with rows
// `stride` pixels apart.
unsigned int& at(unsigned int* out, int stride, const Rect& area, int x, int y) {
    return out[static_cast<long>(y - area.origin.y) * stride + (x - area.origin.x)];
}

}  // namespace

void Compositor::FreePixels::operator()(unsigned int* pixels) const noexcept {
    free(pixels);
}

long Compositor::init() {
    auto count = static_cast<sys::size_t>(scene_.screen.width) * static_cast<sys::size_t>(scene_.screen.height);
    desk_.reset(static_cast<unsigned int*>(malloc(count * sizeof(unsigned int))));
    if (!desk_)
        return SYS_ERROR_FULL;
    refresh_desk(scene_.screen);
    return 0;
}

Rect Compositor::cursor_rect() const {
    return {scene_.pointer, CURSOR_WIDTH, CURSOR_HEIGHT};
}

Rect Compositor::bounds(const Rect& window) {
    if (window.width == 0)
        return window;
    return {{window.origin.x - SHADOW_MARGIN, window.origin.y - SHADOW_MARGIN},
            window.width + 2 * SHADOW_MARGIN,
            window.height + 2 * SHADOW_MARGIN};
}

Rect Compositor::intersect(const Rect& area, const Rect& clip) {
    long left = sys::max<long>(area.origin.x, clip.origin.x);
    long top = sys::max<long>(area.origin.y, clip.origin.y);
    long right =
        sys::min<long>(static_cast<long>(area.origin.x) + area.width, static_cast<long>(clip.origin.x) + clip.width);
    long bottom =
        sys::min<long>(static_cast<long>(area.origin.y) + area.height, static_cast<long>(clip.origin.y) + clip.height);
    if (right <= left || bottom <= top)
        return {};
    return {{static_cast<int>(left), static_cast<int>(top)},
            static_cast<int>(right - left),
            static_cast<int>(bottom - top)};
}

void Compositor::render(const Rect& area, unsigned int* out, int stride) const {
    const int screen_width = scene_.screen.width;
    for (int row = 0; row < area.height; ++row) {
        memcpy(out + static_cast<long>(row) * stride,
               desk_.get() + static_cast<long>(area.origin.y + row) * screen_width + area.origin.x,
               static_cast<sys::size_t>(area.width) * sizeof(unsigned int));
    }
    int top = top_window(scene_.windows, scene_.order);
    for (int slot = MAX_WINDOWS - 1; slot >= 0; --slot) {
        int index = scene_.order[slot];
        const Window& window = scene_.windows[index];
        if (!window.visible() || window.rect.width == 0 || intersect(bounds(window.rect), area).width == 0)
            continue;
        paint_shadow(window.rect, index == top, area, out, stride);
        paint_window(index, index == top, area, out, stride);
    }

    const Rect& outline = scene_.resize_outline;
    Rect part = outline.width != 0 ? intersect(outline, area) : Rect{};
    for (int y = part.origin.y; y < part.origin.y + part.height; ++y) {
        for (int x = part.origin.x; x < part.origin.x + part.width; ++x) {
            int dx = x - outline.origin.x;
            int dy = y - outline.origin.y;
            if (dx < RESIZE_OUTLINE || dy < RESIZE_OUTLINE || dx >= outline.width - RESIZE_OUTLINE ||
                dy >= outline.height - RESIZE_OUTLINE)
                at(out, stride, area, x, y) = theme::ACCENT;
        }
    }

    part = intersect(cursor_rect(), area);
    for (int y = part.origin.y; y < part.origin.y + part.height; ++y) {
        for (int x = part.origin.x; x < part.origin.x + part.width; ++x) {
            char mark = CURSOR[y - scene_.pointer.y][x - scene_.pointer.x];
            if (mark == 'X')
                at(out, stride, area, x, y) = theme::CURSOR_OUTLINE;
            else if (mark == '.')
                at(out, stride, area, x, y) = theme::TEXT;
        }
    }
}

// Darkens what is below a window around its outline. The corner gaps inside
// the outline are shaded as the window is painted.
void Compositor::paint_shadow(const Rect& window, bool focused, const Rect& area, unsigned int* out, int stride) const {
    Rect part = intersect(bounds(window), area);
    int left = window.origin.x;
    int right = left + window.width;
    int top = window.origin.y;
    int bottom = top + window.height;
    for (int y = part.origin.y; y < part.origin.y + part.height; ++y) {
        bool beside = y >= top && y < bottom;
        for (int x = part.origin.x; x < part.origin.x + part.width; ++x) {
            if (beside && x >= left && x < right) {
                x = right - 1;
                continue;
            }
            int shade = shadow(window, {x, y}, focused);
            if (shade != 0) {
                unsigned int& pixel = at(out, stride, area, x, y);
                pixel = mix(pixel, 0, shade);
            }
        }
    }
}

// Paints a window over what is already in `out`. The client surface is
// copied a row at a time; the border, title bar and corners go pixel by pixel.
void Compositor::paint_window(int index, bool focused, const Rect& area, unsigned int* out, int stride) const {
    const Rect& window = scene_.windows[index].rect;
    Rect part = intersect(window, area);
    int part_right = part.origin.x + part.width;
    bool pixels = scene_.windows[index].protocol() == Protocol::Pixels;
    const PixelSurface& surface = scene_.surfaces[index];
    for (int y = part.origin.y; y < part.origin.y + part.height; ++y) {
        int row = y - window.origin.y;
        int edge_y = sys::min(row, window.height - 1 - row);
        // The client pixels of this row that no border or corner touches.
        int run_left = part_right;
        int run_right = part_right;
        if (row >= TITLE_HEIGHT && row < window.height - FRAME) {
            int inset = edge_y >= CORNER_RADIUS ? FRAME : CORNER_RADIUS;
            run_left = sys::max(part.origin.x, window.origin.x + inset);
            run_right = sys::min(part_right, window.origin.x + window.width - inset);
            if (run_left >= run_right)
                run_left = run_right = part_right;
        }
        for (int x = part.origin.x; x < part_right; ++x) {
            if (x != run_left) {
                unsigned int& pixel = at(out, stride, area, x, y);
                pixel = window_pixel(index, focused, {x, y}, pixel);
                continue;
            }
            unsigned int* to = &at(out, stride, area, x, y);
            int count = run_right - run_left;
            int surface_x = run_left - window.origin.x - FRAME;
            int surface_y = row - TITLE_HEIGHT;
            if (!pixels) {
                for (int i = 0; i < count; ++i)
                    to[i] = interior(index, {run_left + i, y}, focused);
            } else if (surface.configured() && surface_y < static_cast<int>(surface.height()) &&
                       surface_x + count <= static_cast<int>(surface.width())) {
                memcpy(to, surface.row(static_cast<unsigned int>(surface_y)) + surface_x,
                       static_cast<sys::size_t>(count) * sizeof(unsigned int));
            } else {
                for (int i = 0; i < count; ++i)
                    to[i] = theme::WINDOW_BACKGROUND;
            }
            x = run_right - 1;
        }
    }
}

// A window's pixel over `below`, the already shaded pixel under it.
unsigned int Compositor::window_pixel(int index, bool focused, Point point, unsigned int below) const {
    const Rect& window = scene_.windows[index].rect;
    unsigned int border = focused ? theme::WINDOW_BORDER_FOCUSED : theme::WINDOW_BORDER;
    int x = point.x - window.origin.x;
    int y = point.y - window.origin.y;
    int edge_x = x < window.width - 1 - x ? x : window.width - 1 - x;
    int edge_y = y < window.height - 1 - y ? y : window.height - 1 - y;
    bool on_border = edge_x == 0 || edge_y == 0;
    if (edge_x >= CORNER_RADIUS || edge_y >= CORNER_RADIUS)
        return on_border ? border : interior(index, point, focused);

    // The border follows the corner one pixel inside the outline.
    int outer = OUTER_CORNER.at(edge_x, edge_y);
    int inner = on_border ? 0 : INNER_CORNER.at(edge_x - 1, edge_y - 1);
    unsigned int pixel = inner == 0 ? border : mix(border, interior(index, point, focused), inner);
    if (outer == 255)
        return pixel;
    return mix(mix(below, 0, shadow(window, point, focused)), pixel, outer);
}

// Redraws the background gradient, dithered so its steps do not show as
// bands, with the menu bar, the desktop icons and the Dock over it.
void Compositor::refresh_desk(const Rect& area) {
    Rect part = intersect(area, scene_.screen);
    if (!desk_)
        return;
    int span = scene_.screen.height > 1 ? scene_.screen.height - 1 : 1;
    for (int y = part.origin.y; y < part.origin.y + part.height; ++y) {
        int level[3];
        for (int channel = 0; channel < 3; ++channel) {
            int shift = 16 - 8 * channel;
            int from = static_cast<int>((theme::DESKTOP_TOP >> shift) & 0xFF);
            int to = static_cast<int>((theme::DESKTOP_BOTTOM >> shift) & 0xFF);
            level[channel] = from * 256 + (to - from) * 256 * y / span;
        }
        unsigned int* row = desk_.get() + static_cast<long>(y) * scene_.screen.width;
        for (int x = part.origin.x; x < part.origin.x + part.width; ++x) {
            int threshold = DITHER[y & 3][x & 3] * 16 + 8;
            unsigned int wallpaper = 0;
            for (int channel = 0; channel < 3; ++channel)
                wallpaper |= static_cast<unsigned int>((level[channel] + threshold) >> 8) << (16 - 8 * channel);
            row[x] = desk({x, y}, wallpaper);
        }
    }
}

// What is behind every window at a point: the menu bar, the desktop icons and
// the Dock over the wallpaper.
unsigned int Compositor::desk(Point point, unsigned int wallpaper) const {
    if (point.y < scene_.menu_height) {
        // A dark line separates the bar from the desktop.
        return point.y == scene_.menu_height - 1 ? theme::CURSOR_OUTLINE
                                                 : scene_.chrome.menu_pixel(scene_.screen.width, point.x, point.y);
    }
    int icon = desktop_icons::item_at(scene_.icons, point);
    if (icon != desktop_icons::NO_ITEM) {
        Rect cell = desktop_icons::cell(scene_.icons, icon);
        return scene_.chrome.icon_pixel(icon, point.x - cell.origin.x, point.y - cell.origin.y, wallpaper);
    }
    const Rect& dock = scene_.dock;
    if (!dock.contains(point))
        return wallpaper;
    unsigned int pixel = theme::TITLE_BACKGROUND;
    int item = desktop_dock::item_at(dock, point);
    if (item != desktop_dock::NO_ITEM) {
        Point origin = desktop_dock::icon_origin(dock, item);
        pixel = scene_.chrome.dock_pixel(item, point.x - origin.x, point.y - origin.y);
    } else {
        // A mark under the icon of every program that has a window.
        for (int candidate = 0; candidate < desktop_dock::ITEM_COUNT; ++candidate) {
            Rect mark = desktop_dock::mark(dock, candidate);
            if (!mark.contains(point))
                continue;
            // Without its corner pixels the small square reads as a dot.
            bool column_edge = point.x == mark.origin.x || point.x == mark.origin.x + mark.width - 1;
            bool row_edge = point.y == mark.origin.y || point.y == mark.origin.y + mark.height - 1;
            if (column_edge && row_edge)
                continue;
            for (const auto& window : scene_.windows) {
                if (window.open() && window.app == desktop_dock::ITEMS[candidate])
                    pixel = theme::TEXT;
            }
        }
    }
    int x = point.x - dock.origin.x;
    int y = point.y - dock.origin.y;
    int edge_x = x < dock.width - 1 - x ? x : dock.width - 1 - x;
    int edge_y = y < dock.height - 1 - y ? y : dock.height - 1 - y;
    int coverage = DOCK_CORNER.at(edge_x, edge_y);
    return coverage == 255 ? pixel : mix(wallpaper, pixel, coverage);
}

// A window's own pixel inside its border.
unsigned int Compositor::interior(int index, Point point, bool focused) const {
    const Rect& window = scene_.windows[index].rect;
    int x = point.x - window.origin.x;
    int y = point.y - window.origin.y;
    if (y < TITLE_HEIGHT) {
        unsigned int pixel = scene_.chrome.title_pixel(index, window.width, x, y);
        return focused ? pixel : mix(pixel, theme::TITLE_INACTIVE, INACTIVE_TITLE);
    }
    if (x < FRAME || x >= window.width - FRAME || y >= window.height - FRAME)
        return theme::WINDOW_BACKGROUND;
    if (scene_.windows[index].protocol() == Protocol::Pixels)
        return scene_.surfaces[index].pixel(x - FRAME, y - TITLE_HEIGHT);
    Point sample = surface_point(window, point);
    return scene_.windows[index].client.frame()[sample.y * SURFACE_SIZE + sample.x];
}

}  // namespace desktop
