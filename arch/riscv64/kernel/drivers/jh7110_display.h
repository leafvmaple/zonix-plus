#pragma once

#include "drivers/display.h"
#include "lib/result.h"

namespace jh7110_display {

// Powers the JH7110 VOUT domain, starts its clocks, drives HDMI at a fixed
// mode from a framebuffer it allocates, and describes that framebuffer.
Error start(display::PlatformFramebuffer& out);

// The DC8200 cursor plane, once start has succeeded.
Error show_cursor(const display::CursorImage& image, uint32_t x, uint32_t y);
void move_cursor(uint32_t x, uint32_t y);
void hide_cursor();

}  // namespace jh7110_display
