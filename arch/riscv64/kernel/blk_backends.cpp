/**
 * @file blk_backends.cpp
 * @brief RISC-V block device backend probe.
 *
 * On RISC-V QEMU virt, block devices are exposed as virtio-blk PCI
 * devices and probed automatically by the generic PCI layer in
 * kernel/drivers/pci.cpp.  No arch-specific work is needed here.
 */

#include "block/blk.h"

namespace blk {

int probe_backends() {
    /* PCI-backed drivers are registered/probed in the global init flow */
    return 0;
}

}  // namespace blk
