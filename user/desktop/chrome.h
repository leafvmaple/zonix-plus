#pragma once

#include "client.h"
#include <sys/memory.hpp>

namespace desktop_chrome {

inline constexpr int TITLE_HEIGHT = 30;
inline constexpr int CLOSE_SIZE = 16;
inline constexpr int CLOSE_TOP = 7;
inline constexpr int LABEL_COLOR = 0xF1F5F9;

inline int close_left(int width) {
    return width - CLOSE_SIZE - 7;
}

class Chrome {
public:
    Chrome();
    ~Chrome();

    long init(int screen_width, const sys::array<int, desktop_ipc::WINDOW_COUNT>& widths);
    long configure(int index, int width);
    unsigned int background() const;
    unsigned int title_pixel(int index, int width, int x, int y) const;

private:
    struct Scene;
    struct SceneDeleter {
        void operator()(Scene* scene) const noexcept;
    };
    sys::unique_ptr<Scene, SceneDeleter> scene_;
};

}  // namespace desktop_chrome
