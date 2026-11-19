#include "drivers/wall_clock.h"

#include "drivers/intr.h"
#include "trap/trap.h"

#include <asm/arch.h>

static_assert(WallClock::from_civil(1970, 1, 1, 0, 0, 0) == 0);
static_assert(WallClock::from_civil(2000, 3, 1, 0, 0, 0) == 951868800);
static_assert(WallClock::from_civil(2024, 2, 29, 23, 59, 59) == 1709251199);
static_assert(WallClock::from_civil(2026, 11, 20, 12, 34, 56) == 1795178096);

Result<uint64_t> WallClock::now() {
    intr::Guard guard;
    const uint64_t uptime_seconds = trap::uptime_ms() / 1000;
    if (state_ == State::Unread) {
        uint64_t seconds = 0;
        // A clock set before the kernel's own uptime is not a time of day.
        if (arch_read_wall_clock(&seconds) && seconds >= uptime_seconds) {
            boot_seconds_ = seconds - uptime_seconds;
            state_ = State::Valid;
        } else {
            state_ = State::Absent;
        }
    }
    ENSURE(state_ == State::Valid, Error::NoDevice);
    return boot_seconds_ + uptime_seconds;
}
