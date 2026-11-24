#pragma once

// USB device model shared by host controller drivers, the core and class
// drivers. A host controller (UsbHost) moves transfers; the core enumerates
// devices and hands each interface to a class driver.

#include <base/types.h>
#include "lib/result.h"

namespace usb {

enum class Speed : uint8_t { Unknown, Low, Full, High, Super };

// Standard request codes and descriptor types (USB 2.0 chapter 9).
inline constexpr uint8_t REQUEST_GET_DESCRIPTOR = 6;
inline constexpr uint8_t REQUEST_SET_CONFIGURATION = 9;
inline constexpr uint8_t DESCRIPTOR_DEVICE = 1;
inline constexpr uint8_t DESCRIPTOR_CONFIGURATION = 2;
inline constexpr uint8_t DESCRIPTOR_INTERFACE = 4;
inline constexpr uint8_t DESCRIPTOR_ENDPOINT = 5;

// bmRequestType: direction, type and recipient.
inline constexpr uint8_t REQUEST_IN = 0x80;
inline constexpr uint8_t REQUEST_CLASS = 0x20;
inline constexpr uint8_t REQUEST_INTERFACE = 0x01;

inline constexpr uint8_t ENDPOINT_IN = 0x80;
inline constexpr uint8_t ENDPOINT_NUMBER_MASK = 0x0F;
inline constexpr uint8_t ENDPOINT_TRANSFER_MASK = 0x03;
inline constexpr uint8_t ENDPOINT_INTERRUPT = 0x03;

struct SetupPacket {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed));
static_assert(sizeof(SetupPacket) == 8);

struct DeviceDescriptor {
    uint8_t length;
    uint8_t type;
    uint16_t usb_version;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t manufacturer;
    uint8_t product;
    uint8_t serial_number;
    uint8_t configuration_count;
} __attribute__((packed));
static_assert(sizeof(DeviceDescriptor) == 18);

struct ConfigurationDescriptor {
    uint8_t length;
    uint8_t type;
    uint16_t total_length;
    uint8_t interface_count;
    uint8_t value;
    uint8_t name;
    uint8_t attributes;
    uint8_t max_power;
} __attribute__((packed));
static_assert(sizeof(ConfigurationDescriptor) == 9);

struct InterfaceDescriptor {
    uint8_t length;
    uint8_t type;
    uint8_t number;
    uint8_t alternate;
    uint8_t endpoint_count;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t name;
} __attribute__((packed));
static_assert(sizeof(InterfaceDescriptor) == 9);

struct EndpointDescriptor {
    uint8_t length;
    uint8_t type;
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
} __attribute__((packed));
static_assert(sizeof(EndpointDescriptor) == 7);

// Called in interrupt context with each completed interrupt IN report.
using ReportHandler = void (*)(void* context, const uint8_t* data, size_t length);

class Host;

// One addressed device on a host controller. The host owns the
// controller-side state behind `slot`.
struct Device {
    Host* host{};
    uint8_t slot{};
    uint8_t port{};
    Speed speed{};
    DeviceDescriptor descriptor{};
};

class Host {
public:
    // Issues a control transfer on endpoint 0. `data` holds setup.length
    // bytes in the direction of setup.request_type.
    virtual Error control(Device& device, const SetupPacket& setup, void* data) = 0;
    // Configures an interrupt IN endpoint and keeps a report request queued
    // on it, calling `handler` with every report the device sends.
    virtual Error start_interrupt_in(Device& device, const EndpointDescriptor& endpoint, ReportHandler handler,
                                     void* context) = 0;

protected:
    ~Host() = default;
};

}  // namespace usb
