#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_hid.h"
#include "drivers/usb/usb_hub.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include "trap/trap.h"
#include <kernel/log.h>

namespace usb {
namespace {

constexpr size_t CONFIGURATION_CAPACITY = 512;
constexpr size_t MAX_INTERFACE_ENDPOINTS = 4;

const char* speed_name(Speed speed) {
    switch (speed) {
        case Speed::Low: return "low";
        case Speed::Full: return "full";
        case Speed::High: return "high";
        case Speed::Super: return "super";
        case Speed::Unknown: break;
    }
    return "unknown";
}

SetupPacket get_descriptor(uint8_t type, uint16_t length) {
    return {REQUEST_IN, REQUEST_GET_DESCRIPTOR, static_cast<uint16_t>(type << 8), 0, length};
}

}  // namespace

Location location(const Device& device) {
    Location out{};
    size_t used = 0;
    auto append = [&](unsigned int value) {
        if (value >= 10)
            out.text[used++] = static_cast<char>('0' + value / 10);
        out.text[used++] = static_cast<char>('0' + value % 10);
    };
    append(device.topology.root_port);
    for (uint8_t tier = 0; tier < device.topology.depth && tier < MAX_HUB_DEPTH; ++tier) {
        out.text[used++] = '.';
        append((device.topology.route >> (4 * tier)) & 0xF);
    }
    out.text[used] = '\0';
    return out;
}

Error Core::read_configuration(Device& device, uint8_t* buffer, size_t capacity, size_t& length) {
    ConfigurationDescriptor head{};
    TRY(device.host->control(device, get_descriptor(DESCRIPTOR_CONFIGURATION, sizeof(head)), &head));
    ENSURE(head.type == DESCRIPTOR_CONFIGURATION && head.total_length >= sizeof(head), Error::Io);
    length = head.total_length < capacity ? head.total_length : capacity;
    TRY(device.host->control(device, get_descriptor(DESCRIPTOR_CONFIGURATION, static_cast<uint16_t>(length)), buffer));
    TRY(device.host->control(device, {0, REQUEST_SET_CONFIGURATION, head.value, 0, 0}, nullptr));
    return Error::None;
}

// Offers every interface's first alternate setting, with its endpoints, to
// the class drivers.
void Core::bind_interfaces(Device& device, const uint8_t* configuration, size_t length) {
    const InterfaceDescriptor* interface = nullptr;
    EndpointDescriptor endpoints[MAX_INTERFACE_ENDPOINTS]{};
    size_t endpoint_count = 0;
    auto offer = [&] {
        if (!interface || interface->alternate != 0)
            return;
        if (hid::attach(device, *interface, endpoints, endpoint_count) == Error::NotSupported)
            static_cast<void>(hub::attach(device, *interface, endpoints, endpoint_count));
    };
    for (size_t offset = 0; offset + 2 <= length;) {
        const uint8_t size = configuration[offset];
        const uint8_t type = configuration[offset + 1];
        if (size < 2 || offset + size > length)
            break;
        if (type == DESCRIPTOR_INTERFACE && size >= sizeof(InterfaceDescriptor)) {
            offer();
            interface = reinterpret_cast<const InterfaceDescriptor*>(configuration + offset);
            endpoint_count = 0;
        } else if (type == DESCRIPTOR_ENDPOINT && size >= sizeof(EndpointDescriptor) && interface &&
                   endpoint_count < MAX_INTERFACE_ENDPOINTS) {
            memcpy(&endpoints[endpoint_count++], configuration + offset, sizeof(EndpointDescriptor));
        }
        offset += size;
    }
    offer();
}

Error Core::attach(Device& device) {
    ENSURE(device.host, Error::Invalid);
    TRY_LOG(
        device.host->control(device, get_descriptor(DESCRIPTOR_DEVICE, sizeof(device.descriptor)), &device.descriptor),
        "usb %s: no device descriptor", location(device).text);
    klog::info("usb %s: %04x:%04x, %s speed\n", location(device).text, device.descriptor.vendor_id,
               device.descriptor.product_id, speed_name(device.speed));
    uint8_t configuration[CONFIGURATION_CAPACITY];
    size_t length = 0;
    TRY_LOG(read_configuration(device, configuration, sizeof(configuration), length),
            "usb %s: cannot configure the device", location(device).text);
    bind_interfaces(device, configuration, length);
    return Error::None;
}

void Core::detach(Device& device) {
    hid::detach(device);
    klog::info("usb %s: removed\n", location(device).text);
}

void Core::port_changed(Host& host, uint8_t hub_slot, uint8_t port) {
    intr::Guard guard;
    for (size_t i = 0; i < event_count_; ++i)
        if (events_[i].host == &host && events_[i].hub_slot == hub_slot && events_[i].port == port)
            return;  // Already queued: the handler reads the port's current state.
    if (event_count_ == events_.size()) {
        klog::warn("usb: port change queue full\n");
        return;
    }
    events_[event_count_++] = {&host, hub_slot, port};
    events_waiting_.wakeup_all();
}

bool Core::next_event(PortEvent& event) {
    intr::Guard guard;
    while (event_count_ == 0)
        events_waiting_.sleep();
    event = events_[0];
    for (size_t i = 1; i < event_count_; ++i)
        events_[i - 1] = events_[i];
    --event_count_;
    return true;
}

void Core::run() {
    for (;;) {
        PortEvent event{};
        if (!next_event(event))
            continue;
        if (event.hub_slot == 0) {
            event.host->root_port_changed(event.port);
            continue;
        }
        // The hub may have left since the change was queued.
        if (Device* hub = event.host->device(event.hub_slot))
            hub::port_changed(*hub, event.port);
    }
}

void Core::sleep_ms(uint64_t ms) {
    intr::Guard guard;
    const uint64_t deadline = trap::uptime_ms() + ms;
    while (trap::uptime_ms() < deadline)
        sleepers_.sleep();
}

void Core::tick() {
    if (!sleepers_.empty())
        sleepers_.wakeup_all();
    hid::tick();
}

}  // namespace usb
