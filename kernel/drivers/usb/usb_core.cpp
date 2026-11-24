#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_hid.h"
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
        if (interface && interface->alternate == 0)
            static_cast<void>(hid::attach(device, *interface, endpoints, endpoint_count));
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
        "usb: port %u: no device descriptor", device.port);
    klog::info("usb: port %u: %04x:%04x, %s speed\n", device.port, device.descriptor.vendor_id,
               device.descriptor.product_id, speed_name(device.speed));
    uint8_t configuration[CONFIGURATION_CAPACITY];
    size_t length = 0;
    TRY_LOG(read_configuration(device, configuration, sizeof(configuration), length),
            "usb: port %u: cannot configure the device", device.port);
    bind_interfaces(device, configuration, length);
    return Error::None;
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
