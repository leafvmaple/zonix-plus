#include "drivers/display.h"

namespace display {

// Firmware hands over the framebuffer, if any; no platform controller is driven.
Error probe_platform(PlatformFramebuffer&) {
    return Error::NoDevice;
}

void flush_platform(uintptr_t, size_t) {}

}  // namespace display
