#include "../chrome.h"

namespace desktop_chrome {

namespace {
constexpr unsigned int LABELS[][7] = {{14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
                                      {31, 4, 4, 4, 4, 4, 4},       {17, 27, 21, 21, 17, 17, 17},
                                      {31, 16, 16, 30, 16, 16, 16}, {31, 16, 16, 30, 16, 16, 31}};
}

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

unsigned int Chrome::background() const {
    return 0x172635;
}

unsigned int Chrome::title_pixel(int index, int width, int x, int y) const {
    int cx = x - close_left(width);
    int cy = y - CLOSE_TOP;
    if (cx >= 0 && cx < CLOSE_SIZE && cy >= 0 && cy < CLOSE_SIZE) {
        if (cx >= 4 && cx < CLOSE_SIZE - 4 && (cx == cy || cx + cy == CLOSE_SIZE - 1))
            return LABEL_COLOR;
        return 0x734551;
    }
    if (x >= 11 && x < 21 && y >= 8 && y < 22 && (LABELS[index][(y - 8) / 2] & (16U >> ((x - 11) / 2))) != 0)
        return LABEL_COLOR;
    return 0x263D51;
}

}  // namespace desktop_chrome
