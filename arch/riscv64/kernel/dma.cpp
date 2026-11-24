#include "mm/dma.h"
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

namespace dma {

// QEMU's devices are coherent; the JH7110's display and PCIe masters are not.
void writeback_invalidate(uintptr_t pa, size_t bytes) {
    constexpr uintptr_t ccache_phys = BOARD_CCACHE_PHYS;
    if constexpr (ccache_phys != 0) {
        const uintptr_t ccache = ccache_phys + KERNEL_BASE;
        arch_mb();
        for (uintptr_t line = pa & ~(CCACHE_LINE - 1); line < pa + bytes; line += CCACHE_LINE)
            mmio::write64(ccache, CCACHE_FLUSH64, line);
        arch_mb();
    } else {
        static_cast<void>(pa);
        static_cast<void>(bytes);
    }
}

}  // namespace dma
