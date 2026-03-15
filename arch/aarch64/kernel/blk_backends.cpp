#include "block/blk.h"

namespace blk {

void probe_backends() {
    // No IDE/AHCI backends on aarch64 for now.
    // Future backends (e.g. TF/eMMC/NVMe) should register here.
}

}  // namespace blk
