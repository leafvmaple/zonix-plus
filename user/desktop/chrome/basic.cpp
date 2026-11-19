#include "../dock.h"

namespace desktop_chrome {

namespace {
namespace theme = zonix_gui::theme;

// One 5x7 letter per program, drawn at twice its size.
constexpr unsigned int LABELS[][7] = {{14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
                                      {31, 4, 4, 4, 4, 4, 4},       {17, 27, 21, 21, 17, 17, 17},
                                      {31, 16, 16, 30, 16, 16, 16}, {31, 16, 16, 30, 16, 16, 31}};
constexpr int LABEL_WIDTH = 10;
constexpr int LABEL_HEIGHT = 14;
constexpr int LABEL_TOP = (TITLE_HEIGHT - LABEL_HEIGHT) / 2;

constexpr unsigned int TILE_COLOR = 0x1A1A1C;

bool inside(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

// A prompt on a dark tile.
unsigned int terminal_icon(int x, int y) {
    using desktop_dock::ICON;
    if (!inside(x, y, 3, 3, ICON - 6, ICON - 6))
        return theme::TITLE_BACKGROUND;
    // A chevron pointing right, then an underscore.
    int dy = y - 20;
    int reach = dy < 0 ? -dy : dy;
    if (reach <= 6 && inside(x, y, 18 - reach, y, 3, 1))
        return theme::TEXT;
    if (inside(x, y, 23, 27, 9, 2))
        return theme::TEXT;
    return TILE_COLOR;
}

unsigned int files_icon(int x, int y) {
    if (inside(x, y, 7, 5, 15, 8) || inside(x, y, 5, 10, 34, 25))
        return theme::FOLDER;
    return theme::TITLE_BACKGROUND;
}

// A ring that is open at the top, with a bar through the gap.
unsigned int exit_icon(int x, int y) {
    int dx = 2 * x + 1 - desktop_dock::ICON;
    int dy = 2 * y + 1 - desktop_dock::ICON;
    int square = dx * dx + dy * dy;
    if (inside(x, y, 21, 12, 2, 11))
        return theme::TEXT;
    if (square <= 16 * 16 && square >= 12 * 12 && !inside(x, y, 18, 0, 8, 22))
        return theme::TEXT;
    return theme::TITLE_BACKGROUND;
}
}  // namespace

struct Chrome::Scene {};

// The basic decorations have no scene allocations.
void Chrome::SceneDeleter::operator()(Scene*) const noexcept {}

Chrome::Chrome() = default;
Chrome::~Chrome() = default;

long Chrome::init(int) {
    return 0;
}

long Chrome::configure(int slot, int app, int) {
    apps_[slot] = app;
    return 0;
}

unsigned int Chrome::title_pixel(int slot, int width, int x, int y) const {
    int index = apps_[slot];
    // The close button is a disc, measured in half pixels from its centre.
    int cx = 2 * (x - CLOSE_LEFT) + 1 - CLOSE_SIZE;
    int cy = 2 * (y - CLOSE_TOP) + 1 - CLOSE_SIZE;
    if (cx * cx + cy * cy <= CLOSE_SIZE * CLOSE_SIZE)
        return theme::CLOSE;
    int lx = x - (width - LABEL_WIDTH) / 2;
    int ly = y - LABEL_TOP;
    if (lx >= 0 && lx < LABEL_WIDTH && ly >= 0 && ly < LABEL_HEIGHT && (LABELS[index][ly / 2] & (16U >> (lx / 2))) != 0)
        return LABEL_COLOR;
    return theme::TITLE_BACKGROUND;
}

unsigned int Chrome::dock_pixel(int item, int x, int y) const {
    switch (desktop_dock::ITEMS[item]) {
        case desktop_ipc::APP_TERMINAL: return terminal_icon(x, y);
        case desktop_ipc::APP_FILES: return files_icon(x, y);
        default: return exit_icon(x, y);
    }
}

}  // namespace desktop_chrome
