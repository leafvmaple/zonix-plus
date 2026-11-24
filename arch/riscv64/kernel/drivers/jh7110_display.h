#pragma once

#include "drivers/display.h"
#include "lib/result.h"

namespace jh7110_display {

// Powers the JH7110 VOUT domain, starts its clocks, drives HDMI at a fixed
// mode from a framebuffer it allocates, and describes that framebuffer.
Error start(display::PlatformFramebuffer& out);

}  // namespace jh7110_display
