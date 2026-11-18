#pragma once

#include <display.h>
#include <sys/array.hpp>

namespace desktop_ipc {

inline constexpr int SURFACE_SIZE = 24;

struct Input {
    DisplayEvent event;
    int x;  // Relative to the client's surface, in pixels.
    int y;
};

using Frame = sys::array<unsigned int, SURFACE_SIZE * SURFACE_SIZE>;

// Both records fit in one atomic nonblocking pipe write.
static_assert(sizeof(Input) == 24 && sizeof(Frame) == 2304);

}  // namespace desktop_ipc
