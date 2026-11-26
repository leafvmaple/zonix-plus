#include "drivers/display.h"

namespace display {

// Firmware hands over the framebuffer, if any; no platform controller is driven.
Error probe_platform(PlatformFramebuffer&) {
    return Error::NoDevice;
}

void flush_platform(uintptr_t, size_t) {}

// Firmware framebuffers come without a cursor plane.
Error show_cursor_platform(const CursorImage&, uint32_t, uint32_t) {
    return Error::NotSupported;
}

Error move_cursor_platform(uint32_t, uint32_t) {
    return Error::NotSupported;
}

void hide_cursor_platform() {}

}  // namespace display
