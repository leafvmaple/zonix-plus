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

}  // namespace dc8200
