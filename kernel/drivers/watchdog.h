#pragma once

#include <base/types.h>
#include "lib/result.h"

// A hardware watchdog that resets the machine when the kernel stops making
// progress or the root disk fails, so that a board nobody can reach starts
// again from its firmware. A kernel thread feeds it while the system is
// healthy; on machines without one, nothing runs.
namespace watchdog {

// Starts the feeder thread. Returns 0 on machines without a watchdog too.
int init();
// Called from the timer interrupt to wake the feeder.
void tick();
// Disarms the watchdog before the machine powers off.
void stop();

// Implemented by each architecture. start_platform measures and arms the
// watchdog to reset after about `seconds` without a feed, returning
// NotSupported on machines without one; feed_platform restarts the period.
Error start_platform(uint32_t seconds);
void feed_platform();
void stop_platform();

}  // namespace watchdog
