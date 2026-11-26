#pragma once

// VeriSilicon DC8200 display controller: one linear XRGB8888 framebuffer on
// the primary plane of display 0, timed for a fixed mode. The controller's
// clocks and resets must already run; the pixel clock must match the mode.

#include <base/types.h>
#include "drivers/display/display_timing.h"

namespace dc8200 {

// `base` maps the controller registers from its first ("hi") region; the
// framebuffer must lie below 4 GiB, its stride a multiple of 4 bytes.
void start(uintptr_t base, const DisplayTiming& timing, uint32_t framebuffer_pa, uint32_t stride);
void stop(uintptr_t base);

// The cursor plane of display 0 shows a CURSOR_SIZE square ARGB8888 image
// with rows CURSOR_SIZE * 4 bytes apart, read from below 4 GiB. Its pixel
// (hot_x, hot_y) lands on (x, y); the hot pixel is at most HOT_LIMIT.
inline constexpr uint32_t CURSOR_SIZE = 32;
inline constexpr uint32_t HOT_LIMIT = 31;
void show_cursor(uintptr_t base, uint32_t image_pa, uint32_t hot_x, uint32_t hot_y, uint32_t x, uint32_t y);
void move_cursor(uintptr_t base, uint32_t x, uint32_t y);
void hide_cursor(uintptr_t base);

}  // namespace dc8200
