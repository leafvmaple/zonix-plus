/**
 * @file display_backends.cpp
 * @brief RISC-V platform display backends.
 *
 * QEMU virt hands over a UEFI framebuffer. The VisionFive 2's firmware does
 * not, so the kernel brings up the JH7110 display pipeline itself.
 */

#include "drivers/display.h"
#include "drivers/jh7110_display.h"
#include "drivers/mmio.h"

#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>

namespace {

// SiFive composable cache: writing a physical address to Flush64 writes the
// 64-byte line back to memory and evicts it, from the inner caches too.
constexpr uintptr_t CCACHE_FLUSH64 = 0x200;
constexpr uintptr_t CCACHE_LINE = 64;

}  // namespace

namespace display {

Error probe_platform(PlatformFramebuffer& out) {
    if constexpr (BOARD_JH7110_DISPLAY) {
        return jh7110_display::start(out);
    } else {
        static_cast<void>(out);
        return Error::NoDevice;
    }
}

void flush_platform(uintptr_t pa, size_t byte_count) {
    constexpr uintptr_t ccache_phys = BOARD_CCACHE_PHYS;
    if constexpr (ccache_phys != 0) {
        const uintptr_t ccache = ccache_phys + KERNEL_BASE;
        arch_mb();
        for (uintptr_t line = pa & ~(CCACHE_LINE - 1); line < pa + byte_count; line += CCACHE_LINE)
            mmio::write64(ccache, CCACHE_FLUSH64, line);
        arch_mb();
    } else {
        static_cast<void>(pa);
        static_cast<void>(byte_count);
    }
}

}  // namespace display
