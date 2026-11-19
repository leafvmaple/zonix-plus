#include "../chrome.h"

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

}  // namespace desktop_chrome
