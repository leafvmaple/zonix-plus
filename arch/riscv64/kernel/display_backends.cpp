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

}  // namespace display
