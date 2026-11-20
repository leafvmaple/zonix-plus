#pragma once

#include <display.h>
#include <sys/array.hpp>

namespace desktop_ipc {

inline constexpr int SURFACE_SIZE = 24;
// Set by Desktop on the second left-button release of a double click.
inline constexpr unsigned int INPUT_MOD_DOUBLE_CLICK = 1U << 16;
// Desktop-only event. The value is a negative error from an edit request.
inline constexpr unsigned int INPUT_EDIT_ERROR = 0x10000;
// Desktop-only event. The user asked to close a window whose client decides
// when to exit, so it can first ask about unsaved work.
inline constexpr unsigned int INPUT_CLOSE_REQUEST = 0x10001;
// Desktop-only event. The user resized the window; x and y are the surface
// size Desktop proposes. A client that can resize answers with a Size request
// for the size it accepts, which may be clamped, followed by a full update.
// Others ignore it and keep their size.
inline constexpr unsigned int INPUT_RESIZE = 0x10002;

struct Input {
    DisplayEvent event;
    int x;  // Relative to the client's surface, in pixels.
    int y;
};

using Frame = sys::array<unsigned int, SURFACE_SIZE * SURFACE_SIZE>;

// Both records fit in one atomic nonblocking pipe write.
static_assert(sizeof(Input) == 24 && sizeof(Frame) == 2304);

}  // namespace desktop_ipc
