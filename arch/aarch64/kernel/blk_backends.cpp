#include "block/blk.h"

namespace blk {

int probe_backends() {
    // No IDE/AHCI backends on aarch64 for now.
    // Future backends (e.g. TF/eMMC/NVMe) should register here.
    return 0;
}

}  // namespace blk
