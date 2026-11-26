#include "drivers/entropy.h"

namespace entropy {

// No platform generator is driven on this architecture's machines; the VirtIO
// entropy device is the source.
Error start_platform() {
    return Error::NotSupported;
}

Result<size_t> read_platform(void*, size_t) {
    return Error::NoDevice;
}

}  // namespace entropy
