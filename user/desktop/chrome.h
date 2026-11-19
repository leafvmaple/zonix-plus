#pragma once

#include "client.h"
#include "../gui/theme.h"
#include <sys/memory.hpp>

namespace desktop_chrome {

inline constexpr int TITLE_HEIGHT = 30;
// The close button is a disc that keeps the same distance from the top, bottom
// and left of the title bar. The title is centred in the bar.
inline constexpr int CLOSE_SIZE = 14;
inline constexpr int CLOSE_MARGIN = (TITLE_HEIGHT - CLOSE_SIZE) / 2;
inline constexpr int CLOSE_TOP = CLOSE_MARGIN;
inline constexpr int CLOSE_LEFT = CLOSE_MARGIN;
inline constexpr int LABEL_COLOR = zonix_gui::theme::TEXT;

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
