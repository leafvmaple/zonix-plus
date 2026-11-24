#pragma once

#include "drivers/usb/usb.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>

namespace usb {

// Enumeration, class-driver binding and port changes. Host controller
// drivers address a new device, then call attach from the USB thread, where
// waits may sleep. Ports that change while the system runs are queued with
// port_changed, from any context, and handled by that thread in run.
class Core {
public:
    static Error attach(Device& device);
    // Tells the class drivers a device is leaving, before its host frees it.
    static void detach(Device& device);
    // Records that a port changed: a root port when `hub_slot` is 0,
    // otherwise a port of the hub in that slot.
    static void port_changed(Host& host, uint8_t hub_slot, uint8_t port);
    // The USB thread's loop: handles queued port changes forever.
    [[noreturn]] static void run();
    // Sleeps the calling kernel thread for at least `ms` milliseconds.
    static void sleep_ms(uint64_t ms);
    // Timer tick: wakes sleepers and runs class-driver timers.
    static void tick();

private:
    struct PortEvent {
        Host* host;
        uint8_t hub_slot;
        uint8_t port;
    };

    static Error read_configuration(Device& device, uint8_t* buffer, size_t capacity, size_t& length);
    static void bind_interfaces(Device& device, const uint8_t* configuration, size_t length);
    static bool next_event(PortEvent& event);

    inline static WaitQueue sleepers_{};
    inline static WaitQueue events_waiting_{};
    inline static sys::array<PortEvent, 32> events_{};
    inline static size_t event_count_{};
};

}  // namespace usb
