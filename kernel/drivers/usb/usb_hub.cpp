#include "drivers/usb/usb_hub.h"
#include "drivers/usb/usb_core.h"
#include <kernel/log.h>

namespace usb::hub {
namespace {

constexpr uint8_t CLASS_HUB = 9;
constexpr uint8_t PROTOCOL_MULTI_TT = 2;  // bDeviceProtocol of a multi-TT high-speed hub
constexpr uint8_t DESCRIPTOR_HUB = 0x29;

constexpr uint8_t REQUEST_GET_STATUS = 0;
constexpr uint8_t REQUEST_CLEAR_FEATURE = 1;
constexpr uint8_t REQUEST_SET_FEATURE = 3;
constexpr uint8_t TYPE_HUB_IN = REQUEST_IN | REQUEST_CLASS;  // Device recipient
constexpr uint8_t TYPE_PORT_IN = REQUEST_IN | REQUEST_CLASS | 0x03;
constexpr uint8_t TYPE_PORT_OUT = REQUEST_CLASS | 0x03;

constexpr uint16_t FEATURE_PORT_RESET = 4;
constexpr uint16_t FEATURE_PORT_POWER = 8;
constexpr uint16_t FEATURE_C_PORT_CONNECTION = 16;
constexpr uint16_t FEATURE_C_PORT_RESET = 20;
// Change bits 0-4 are cleared with features 16-20 (connection, enable,
// suspend, over-current, reset).
constexpr uint16_t FEATURE_FIRST_CHANGE = 16;
constexpr uint16_t CHANGE_BITS = 5;

constexpr uint16_t STATUS_CONNECTION = 1U << 0;
constexpr uint16_t STATUS_ENABLE = 1U << 1;
constexpr uint16_t STATUS_OVER_CURRENT = 1U << 3;
constexpr uint16_t STATUS_POWER = 1U << 8;
constexpr uint16_t STATUS_LOW_SPEED = 1U << 9;
constexpr uint16_t STATUS_HIGH_SPEED = 1U << 10;
constexpr uint16_t CHANGE_CONNECTION = 1U << 0;
constexpr uint16_t CHANGE_OVER_CURRENT = 1U << 3;
constexpr uint16_t CHANGE_RESET = 1U << 4;

constexpr uint16_t CHARACTERISTICS_THINK_TIME_SHIFT = 5;
constexpr uint64_t RESET_TIMEOUT_MS = 500;
constexpr uint64_t RESET_RECOVERY_MS = 20;
constexpr uint64_t MIN_POWER_GOOD_MS = 100;
// Devices signal their presence after power; ports are watched this long and
// accepted once their states have held still.
constexpr uint64_t CONNECT_WINDOW_MS = 1500;
constexpr uint64_t CONNECT_POLL_MS = 50;
constexpr uint64_t CONNECT_STABLE_MS = 400;
// A newly connected device must stay connected this long before its reset.
constexpr uint64_t DEBOUNCE_MS = 100;

struct HubDescriptor {
    uint8_t length;
    uint8_t type;
    uint8_t port_count;
    uint16_t characteristics;
    uint8_t power_good_2ms;  // Port power-on to power-good, in 2 ms units
    uint8_t control_current;
} __attribute__((packed));

struct PortStatus {
    uint16_t status;
    uint16_t change;
} __attribute__((packed));

Error port_feature(Device& hub, uint8_t request, uint16_t feature, uint8_t port) {
    return hub.host->control(hub, {TYPE_PORT_OUT, request, feature, port, 0}, nullptr);
}

Error port_status(Device& hub, uint8_t port, PortStatus& status) {
    return hub.host->control(hub, {TYPE_PORT_IN, REQUEST_GET_STATUS, 0, port, sizeof(status)}, &status);
}

// Resets the port and reports the speed of the device left enabled on it.
Error reset_port(Device& hub, uint8_t port, Speed& speed) {
    TRY(port_feature(hub, REQUEST_SET_FEATURE, FEATURE_PORT_RESET, port));
    PortStatus status{};
    for (uint64_t waited = 0;; waited += 10) {
        Core::sleep_ms(10);
        TRY(port_status(hub, port, status));
        if ((status.change & CHANGE_RESET) != 0)
            break;
        ENSURE(waited < RESET_TIMEOUT_MS, Error::Timeout);
    }
    TRY(port_feature(hub, REQUEST_CLEAR_FEATURE, FEATURE_C_PORT_RESET, port));
    TRY(port_feature(hub, REQUEST_CLEAR_FEATURE, FEATURE_C_PORT_CONNECTION, port));
    Core::sleep_ms(RESET_RECOVERY_MS);
    ENSURE((status.status & STATUS_CONNECTION) != 0 && (status.status & STATUS_ENABLE) != 0, Error::Io);
    speed = (status.status & STATUS_LOW_SPEED) != 0    ? Speed::Low
            : (status.status & STATUS_HIGH_SPEED) != 0 ? Speed::High
                                                       : Speed::Full;
    return Error::None;
}

// Each status report is a bitmap: bit 0 for the hub, bit n for port n.
void status_report(void* context, const uint8_t* data, size_t length) {
    auto* hub = static_cast<Device*>(context);
    for (uint8_t port = 1; port < 16 && port / 8 < length; ++port)
        if ((data[port / 8] & (1U << (port % 8))) != 0)
            Core::port_changed(*hub->host, hub->slot, port);
}

}  // namespace

void port_changed(Device& hub, uint8_t port) {
    PortStatus status{};
    if (port_status(hub, port, status) != Error::None)
        return;
    for (uint16_t bit = 0; bit < CHANGE_BITS; ++bit)
        if ((status.change & (1U << bit)) != 0)
            static_cast<void>(port_feature(hub, REQUEST_CLEAR_FEATURE, FEATURE_FIRST_CHANGE + bit, port));
    // The hub cuts power to a port that drew too much current; give it back
    // once the condition has cleared. Hubs also report a passing over-current
    // change when their ports are first powered, so only one that persists is
    // a warning.
    if ((status.change & CHANGE_OVER_CURRENT) != 0) {
        if ((status.status & STATUS_OVER_CURRENT) != 0)
            klog::warn("usb %s.%u: over-current\n", location(hub).text, port);
        if ((status.status & (STATUS_OVER_CURRENT | STATUS_POWER)) == 0) {
            static_cast<void>(port_feature(hub, REQUEST_SET_FEATURE, FEATURE_PORT_POWER, port));
            Core::sleep_ms(MIN_POWER_GOOD_MS);
            if (port_status(hub, port, status) != Error::None)
                return;
            klog::debug("usb %s.%u: powered again, status 0x%x\n", location(hub).text, port, status.status);
        }
    }
    Device* child = hub.host->child(hub.slot, port);
    const bool connected = (status.status & STATUS_CONNECTION) != 0;
    if (child && (!connected || (status.change & CHANGE_CONNECTION) != 0)) {
        hub.host->remove_device(*child);
        child = nullptr;
    }
    if (!connected || child)
        return;
    Core::sleep_ms(DEBOUNCE_MS);
    if (port_status(hub, port, status) != Error::None || (status.status & STATUS_CONNECTION) == 0)
        return;
    Speed speed{};
    Error error = reset_port(hub, port, speed);
    if (error == Error::None)
        error = hub.host->add_device(hub, port, speed);
    if (error != Error::None)
        klog::warn("usb %s.%u: device not usable (%d)\n", location(hub).text, port, static_cast<int>(error));
}

Error attach(Device& device, const InterfaceDescriptor& interface, const EndpointDescriptor* endpoints,
             size_t endpoint_count) {
    ENSURE(interface.interface_class == CLASS_HUB, Error::NotSupported);
    ENSURE_LOG(device.speed != Speed::Super, Error::NotSupported, "usb %s: USB 3 hubs are not supported",
               location(device).text);
    ENSURE_LOG(device.topology.depth < MAX_HUB_DEPTH, Error::NotSupported, "usb %s: hubs nested too deep",
               location(device).text);
    HubDescriptor descriptor{};
    TRY(device.host->control(
        device,
        {TYPE_HUB_IN, REQUEST_GET_DESCRIPTOR, static_cast<uint16_t>(DESCRIPTOR_HUB << 8), 0, sizeof(descriptor)},
        &descriptor));
    ENSURE(descriptor.type == DESCRIPTOR_HUB && descriptor.port_count != 0 && descriptor.port_count <= 15,
           Error::NotSupported);
    const auto think_time = static_cast<uint8_t>((descriptor.characteristics >> CHARACTERISTICS_THINK_TIME_SHIFT) & 3);
    const bool multi_tt = device.descriptor.device_protocol == PROTOCOL_MULTI_TT;
    TRY(device.host->set_hub(device, descriptor.port_count, think_time, multi_tt));
    klog::info("usb %s: hub with %u ports\n", location(device).text, descriptor.port_count);

    for (uint8_t port = 1; port <= descriptor.port_count; ++port)
        static_cast<void>(port_feature(device, REQUEST_SET_FEATURE, FEATURE_PORT_POWER, port));
    const uint64_t power_good = descriptor.power_good_2ms * 2ULL;
    Core::sleep_ms(power_good > MIN_POWER_GOOD_MS ? power_good : MIN_POWER_GOOD_MS);

    // A device needs time after power reaches it before it signals its
    // presence; keyboards take longer than many devices. Watch the ports
    // until each has kept its connection state for a while.
    uint16_t connected = 0;
    uint64_t stable_since = 0;
    for (uint64_t waited = 0; waited < CONNECT_WINDOW_MS; waited += CONNECT_POLL_MS) {
        uint16_t now = 0;
        for (uint8_t port = 1; port <= descriptor.port_count; ++port) {
            PortStatus status{};
            if (port_status(device, port, status) == Error::None && (status.status & STATUS_CONNECTION) != 0)
                now = static_cast<uint16_t>(now | (1U << port));
        }
        if (now != connected) {
            connected = now;
            stable_since = waited;
        } else if (now != 0 && waited - stable_since >= CONNECT_STABLE_MS) {
            break;
        }
        Core::sleep_ms(CONNECT_POLL_MS);
    }
    for (uint8_t port = 1; port <= descriptor.port_count; ++port) {
        PortStatus status{};
        if (port_status(device, port, status) == Error::None)
            klog::debug("usb %s.%u: status 0x%x change 0x%x\n", location(device).text, port, status.status,
                        status.change);
        port_changed(device, port);
    }

    // Later arrivals, departures and over-current come as status reports.
    for (size_t i = 0; i < endpoint_count; ++i)
        if ((endpoints[i].address & ENDPOINT_IN) != 0 &&
            (endpoints[i].attributes & ENDPOINT_TRANSFER_MASK) == ENDPOINT_INTERRUPT)
            return device.host->start_in(device, endpoints[i], status_report, &device);
    klog::warn("usb %s: hub has no status endpoint; later changes are missed\n", location(device).text);
    return Error::None;
}

}  // namespace usb::hub
