#pragma once

#include "protocol.h"

namespace desktop_ipc {

// The console font's cell, and the grid Terminal opens with. Text Editor
// opens at the same size.
inline constexpr int TERMINAL_COLS = 64;
inline constexpr int TERMINAL_ROWS = 20;
inline constexpr int GLYPH_WIDTH = 8;
inline constexpr int GLYPH_HEIGHT = 16;
inline constexpr int TERMINAL_WIDTH = TERMINAL_COLS * GLYPH_WIDTH;
inline constexpr int TERMINAL_HEIGHT = TERMINAL_ROWS * GLYPH_HEIGHT;

}  // namespace desktop_ipc
