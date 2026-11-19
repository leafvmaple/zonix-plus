#include "../chrome.h"

namespace desktop_chrome {

namespace {
namespace theme = zonix_gui::theme;

// One 5x7 letter per window, drawn at twice its size.
constexpr unsigned int LABELS[][7] = {{14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
                                      {31, 4, 4, 4, 4, 4, 4},       {17, 27, 21, 21, 17, 17, 17},
                                      {31, 16, 16, 30, 16, 16, 16}, {31, 16, 16, 30, 16, 16, 31}};
constexpr int LABEL_WIDTH = 10;
constexpr int LABEL_HEIGHT = 14;
constexpr int LABEL_TOP = (TITLE_HEIGHT - LABEL_HEIGHT) / 2;
constexpr int CROSS_INSET = 5;
}  // namespace

struct Chrome::Scene {};

// The basic decorations have no scene allocations.
void Chrome::SceneDeleter::operator()(Scene*) const noexcept {}

Chrome::Chrome() = default;
Chrome::~Chrome() = default;

long Chrome::init(int, const sys::array<int, desktop_ipc::WINDOW_COUNT>&) {
    return 0;
}

long Chrome::configure(int, int) {
    return 0;
}

unsigned int Chrome::title_pixel(int index, int width, int x, int y) const {
    int cx = x - close_left(width);
    int cy = y - CLOSE_TOP;
    if (cx >= 0 && cx < CLOSE_SIZE && cy >= 0 && cy < CLOSE_SIZE) {
        bool corner = (cx == 0 || cx == CLOSE_SIZE - 1) && (cy == 0 || cy == CLOSE_SIZE - 1);
        if (!corner) {
            if (cx >= CROSS_INSET && cx < CLOSE_SIZE - CROSS_INSET && (cx == cy || cx + cy == CLOSE_SIZE - 1))
                return LABEL_COLOR;
            return theme::CLOSE;
        }
    }
    int lx = x - TITLE_INSET;
    int ly = y - LABEL_TOP;
    if (lx >= 0 && lx < LABEL_WIDTH && ly >= 0 && ly < LABEL_HEIGHT && (LABELS[index][ly / 2] & (16U >> (lx / 2))) != 0)
        return LABEL_COLOR;
    return y == TITLE_HEIGHT - 1 ? theme::TITLE_DIVIDER : theme::TITLE_BACKGROUND;
}

}  // namespace desktop_chrome
