#include "block/blk.h"

namespace blk {

int probe_backends() {
    // PCI-backed drivers are registered/probed in the global init flow.
    return 0;
}

}  // namespace blk
