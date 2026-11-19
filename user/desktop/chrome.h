#pragma once

#include "client.h"
#include "../gui/theme.h"
#include <sys/memory.hpp>

namespace desktop_chrome {

inline constexpr int TITLE_HEIGHT = 30;
inline constexpr int TITLE_INSET = 12;
// The close button keeps the same distance from the top, bottom and right of the title bar.
inline constexpr int CLOSE_SIZE = 18;
inline constexpr int CLOSE_MARGIN = (TITLE_HEIGHT - CLOSE_SIZE) / 2;
inline constexpr int CLOSE_TOP = CLOSE_MARGIN;
inline constexpr int LABEL_COLOR = zonix_gui::theme::TEXT;

inline int close_left(int width) {
    return width - CLOSE_SIZE - CLOSE_MARGIN;
}

// Draws each window's title bar. The compositor adds the border, rounds the
// corners and dims unfocused windows.
class Chrome {
public:
    Chrome();
    ~Chrome();

    long init(int screen_width, const sys::array<int, desktop_ipc::WINDOW_COUNT>& widths);
    long configure(int index, int width);
    unsigned int title_pixel(int index, int width, int x, int y) const;

private:
    struct Scene;
    struct SceneDeleter {
        void operator()(Scene* scene) const noexcept;
    };
    sys::unique_ptr<Scene, SceneDeleter> scene_;
};

}  // namespace desktop_chrome
