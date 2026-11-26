/**
 * @file display_backends.cpp
 * @brief RISC-V platform display backends.
 *
 * QEMU virt hands over a UEFI framebuffer. The VisionFive 2's firmware does
 * not, so the kernel brings up the JH7110 display pipeline itself.
 */

#include "drivers/display.h"
#include "drivers/jh7110_display.h"
#include "mm/dma.h"

#include <asm/board.h>

namespace display {

Error probe_platform(PlatformFramebuffer& out) {
    if constexpr (BOARD_JH7110_DISPLAY) {
        return jh7110_display::start(out);
    } else {
        static_cast<void>(out);
        return Error::NoDevice;
    }
}

// The display controller reads memory around the CPU caches.
void flush_platform(uintptr_t pa, size_t byte_count) {
    dma::writeback_invalidate(pa, byte_count);
}

// The JH7110's DC8200 has a cursor plane; QEMU's firmware framebuffer has none.
Error show_cursor_platform(const CursorImage& image, uint32_t x, uint32_t y) {
    if constexpr (BOARD_JH7110_DISPLAY) {
        return jh7110_display::show_cursor(image, x, y);
    } else {
        static_cast<void>(image);
        static_cast<void>(x);
        static_cast<void>(y);
        return Error::NotSupported;
    }
}

Error move_cursor_platform(uint32_t x, uint32_t y) {
    if constexpr (BOARD_JH7110_DISPLAY) {
        jh7110_display::move_cursor(x, y);
        return Error::None;
    } else {
        static_cast<void>(x);
        static_cast<void>(y);
        return Error::NotSupported;
    }
}

void hide_cursor_platform() {
    if constexpr (BOARD_JH7110_DISPLAY)
        jh7110_display::hide_cursor();
}

}  // namespace display
