/**
 * @file blk_backends.cpp
 * @brief RISC-V block device backend probe.
 *
 * On QEMU virt, block devices are virtio-blk and SDHCI PCI devices probed by
 * the generic PCI layer. Boards with an on-chip DesignWare SD controller
 * (VisionFive 2) name it in their board header and attach it here.
 */

#include "block/blk.h"
#include "drivers/sd/dw_mmc.h"

#include <asm/board.h>

namespace blk {

int probe_backends() {
    constexpr uintptr_t sd_phys = BOARD_SD_PHYS;
    if constexpr (sd_phys != 0) {
        // A missing or unreadable card leaves the board without that disk only.
        static_cast<void>(dw_mmc::Manager::attach(sd_phys, BOARD_SD_CLOCK_HZ));
    }
    return 0;
}

}  // namespace blk
