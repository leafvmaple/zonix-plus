#pragma once

#include <display.h>
#include <sys/array.hpp>

namespace desktop_ipc {

inline constexpr int SURFACE_SIZE = 24;
// Set by Desktop on the second left-button release of a double click.
inline constexpr unsigned int INPUT_MOD_DOUBLE_CLICK = 1U << 16;
// Desktop-only event. The value is a negative error from an edit request.
inline constexpr unsigned int INPUT_EDIT_ERROR = 0x10000;

struct Input {
    DisplayEvent event;
    int x;  // Relative to the client's surface, in pixels.
    int y;
};

using Frame = sys::array<unsigned int, SURFACE_SIZE * SURFACE_SIZE>;

// Both records fit in one atomic nonblocking pipe write.
static_assert(sizeof(Input) == 24 && sizeof(Frame) == 2304);

}  // namespace desktop_ipc
