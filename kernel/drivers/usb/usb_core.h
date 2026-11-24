#pragma once

#include "drivers/usb/usb.h"
#include "lib/waitqueue.h"

namespace usb {

// Enumeration and class-driver binding. Host controller drivers address a
// new device, then call attach from a context that may sleep.
class Core {
public:
    static Error attach(Device& device);
    // Sleeps the calling kernel thread for at least `ms` milliseconds.
    static void sleep_ms(uint64_t ms);
    // Timer tick: wakes sleepers and runs class-driver timers.
    static void tick();

private:
    static Error read_configuration(Device& device, uint8_t* buffer, size_t capacity, size_t& length);
    static void bind_interfaces(Device& device, const uint8_t* configuration, size_t length);

    inline static WaitQueue sleepers_{};
};

}  // namespace usb
