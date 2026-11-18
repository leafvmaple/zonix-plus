#pragma once

#include "protocol.h"

namespace desktop_ipc {

inline constexpr int TERMINAL_COLS = 64;
inline constexpr int TERMINAL_ROWS = 20;
inline constexpr int GLYPH_WIDTH = 8;
inline constexpr int GLYPH_HEIGHT = 16;
inline constexpr int TERMINAL_WIDTH = TERMINAL_COLS * GLYPH_WIDTH;
inline constexpr int TERMINAL_HEIGHT = TERMINAL_ROWS * GLYPH_HEIGHT;
inline constexpr int CURSOR_ROW = TERMINAL_COLS * TERMINAL_ROWS;
inline constexpr int CURSOR_COL = CURSOR_ROW + 1;
inline constexpr int CURSOR_VISIBLE = CURSOR_ROW + 2;

struct TextDamage {
    int left = TERMINAL_COLS;
    int top = TERMINAL_ROWS;
    int right{};
    int bottom{};

    void include(int x, int y) {
        if (x >= TERMINAL_COLS || y >= TERMINAL_ROWS)
            return;
        if (x < left)
            left = x;
        if (y < top)
            top = y;
        if (x >= right)
            right = x + 1;
        if (y >= bottom)
            bottom = y + 1;
    }
    bool empty() const { return left >= right; }
};

inline TextDamage compare_text(const Frame& old_frame, const Frame& next_frame) {
    TextDamage damage;
    const auto* old = reinterpret_cast<const unsigned char*>(old_frame.data());
    const auto* next = reinterpret_cast<const unsigned char*>(next_frame.data());
    for (int cell = 0; cell < TERMINAL_COLS * TERMINAL_ROWS; ++cell)
        if (old[cell] != next[cell])
            damage.include(cell % TERMINAL_COLS, cell / TERMINAL_COLS);
    if (old[CURSOR_VISIBLE] != next[CURSOR_VISIBLE] || old[CURSOR_ROW] != next[CURSOR_ROW] ||
        old[CURSOR_COL] != next[CURSOR_COL]) {
        if (old[CURSOR_VISIBLE])
            damage.include(old[CURSOR_COL], old[CURSOR_ROW]);
        if (next[CURSOR_VISIBLE])
            damage.include(next[CURSOR_COL], next[CURSOR_ROW]);
    }
    return damage;
}

static_assert(CURSOR_VISIBLE < sizeof(Frame));

}  // namespace desktop_ipc
