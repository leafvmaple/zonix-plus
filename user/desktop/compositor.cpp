#include "compositor.h"
#include "../gui/shape.h"
#include "../gui/theme.h"

namespace desktop {

namespace {

using namespace desktop_ipc;
using desktop_chrome::LABEL_COLOR;
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

constexpr int CORNER_RADIUS = 6;
constexpr CornerMask<CORNER_RADIUS> OUTER_CORNER{};
constexpr CornerMask<CORNER_RADIUS - 1> INNER_CORNER{};
// The shadow is the window's outline moved down and faded over the blur distance.
constexpr int SHADOW_OFFSET = 4;
constexpr int SHADOW_BLUR = 12;
constexpr int SHADOW_MARGIN = SHADOW_BLUR + SHADOW_OFFSET;
constexpr int SHADOW_FOCUSED = 128;
constexpr int SHADOW_UNFOCUSED = 72;
// How far an unfocused title bar moves towards theme::TITLE_INACTIVE, out of 255.
constexpr int INACTIVE_TITLE = 120;
constexpr int CURSOR_WIDTH = 12;
constexpr int CURSOR_HEIGHT = 19;
// 'X' is the pointer's outline and '.' its fill. The hot spot is the top-left pixel.
constexpr const char* CURSOR[CURSOR_HEIGHT] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ",
    "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX", "X...X..X    ", "X..XX..X    ",
    "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ", "       XX   "};

// Blends towards `to` by `amount` out of 255.
unsigned int mix(unsigned int from, unsigned int to, int amount) {
    unsigned int weight = static_cast<unsigned int>(amount + (amount >> 7));
    unsigned int red_blue = (((from & 0xFF00FF) * (256 - weight) + (to & 0xFF00FF) * weight) >> 8) & 0xFF00FF;
    unsigned int green = (((from & 0x00FF00) * (256 - weight) + (to & 0x00FF00) * weight) >> 8) & 0x00FF00;
    return red_blue | green;
}

// Shadow darkness out of 255 at a point outside the window or in its corner gaps.
int shadow(const Rect& window, Point point, int strength) {
    int left = window.origin.x;
    int right = left + window.width - 1;
    int top = window.origin.y + SHADOW_OFFSET;
    int bottom = top + window.height - 1;
    int dx = point.x < left ? left - point.x : (point.x > right ? point.x - right : 0);
    int dy = point.y < top ? top - point.y : (point.y > bottom ? point.y - bottom : 0);
    if (dx >= SHADOW_BLUR || dy >= SHADOW_BLUR)
        return 0;
    int square = dx * dx + dy * dy;
    int distance = 0;
    while ((distance + 1) * (distance + 1) <= square)
        ++distance;
    if (distance >= SHADOW_BLUR)
        return 0;
    int fade = SHADOW_BLUR - distance;
    return strength * fade * fade / (SHADOW_BLUR * SHADOW_BLUR);
}

}  // namespace

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

unsigned int Compositor::color(Point point) const {
    if (cursor_rect().contains(point)) {
        char mark = CURSOR[point.y - scene_.pointer.y][point.x - scene_.pointer.x];
        if (mark == 'X')
            return theme::CURSOR_OUTLINE;
        if (mark == '.')
            return theme::TEXT;
    }
    return compose(point, 0, top_window(scene_.windows, scene_.order));
}

// The colour of the windows from `slot` down in the stacking order, over the background.
unsigned int Compositor::compose(Point point, int slot, int top) const {
    for (; slot < WINDOW_COUNT; ++slot) {
        int index = scene_.order[slot];
        const Rect& window = scene_.windows[index].rect;
        if (!scene_.windows[index].open() || window.width == 0)
            continue;
        bool focused = index == top;
        int strength = focused ? SHADOW_FOCUSED : SHADOW_UNFOCUSED;
        if (!window.contains(point)) {
            int shade = shadow(window, point, strength);
            if (shade != 0)
                return mix(compose(point, slot + 1, top), 0, shade);
            continue;
        }
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
        unsigned int below = mix(compose(point, slot + 1, top), 0, shadow(window, point, strength));
        return mix(below, pixel, outer);
    }
    return background(point);
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
    if (is_text_window(index))
        return text_color(index, x - FRAME, y - TITLE_HEIGHT);
    if (is_pixel_window(index))
        return scene_.surfaces[index - FIRST_PIXEL_WINDOW].pixel(x - FRAME, y - TITLE_HEIGHT);
    Point sample = surface_point(window, point);
    return scene_.windows[index].client.frame()[sample.y * SURFACE_SIZE + sample.x];
}

// A vertical gradient, dithered so its steps do not show as bands.
unsigned int Compositor::background(Point point) const {
    static constexpr unsigned char dither[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    int threshold = dither[point.y & 3][point.x & 3] * 16 + 8;
    int span = scene_.screen.height > 1 ? scene_.screen.height - 1 : 1;
    unsigned int pixel = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        int from = static_cast<int>((theme::DESKTOP_TOP >> shift) & 0xFF);
        int to = static_cast<int>((theme::DESKTOP_BOTTOM >> shift) & 0xFF);
        int level = from * 256 + (to - from) * 256 * point.y / span;
        pixel |= static_cast<unsigned int>((level + threshold) >> 8) << shift;
    }
    return pixel;
}

unsigned int Compositor::text_color(int index, int x, int y) const {
    const auto* data = reinterpret_cast<const unsigned char*>(scene_.windows[index].client.frame().data());
    int row = y / GLYPH_HEIGHT;
    int col = x / GLYPH_WIDTH;
    if (data[CURSOR_VISIBLE] && row == data[CURSOR_ROW] && col == data[CURSOR_COL] &&
        y % GLYPH_HEIGHT >= GLYPH_HEIGHT - 2)
        return theme::ACCENT;
    auto* glyph = psf::glyph(&scene_.font, data[row * TERMINAL_COLS + col]);
    return (glyph[y % GLYPH_HEIGHT] & (0x80U >> (x % GLYPH_WIDTH))) != 0 ? LABEL_COLOR : theme::WINDOW_BACKGROUND;
}

}  // namespace desktop
