#include "drivers/watchdog.h"

namespace watchdog {

// No platform watchdog is driven on this architecture's machines.
Error start_platform(uint32_t) {
    return Error::NotSupported;
}

void feed_platform() {}

void stop_platform() {}

}  // namespace watchdog
