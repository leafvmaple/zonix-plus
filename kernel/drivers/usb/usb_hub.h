#pragma once

// USB 2.0 hubs: powers each downstream port, enumerates the devices found
// there, then follows the hub's status reports to add devices that arrive,
// remove those that leave and restore power after over-current.

#include "drivers/usb/usb.h"

namespace usb::hub {

// Claims a hub interface; NotSupported for others.
Error attach(Device& device, const InterfaceDescriptor& interface, const EndpointDescriptor* endpoints,
             size_t endpoint_count);
// Brings one port of `hub` up to date. Runs in the USB thread.
void port_changed(Device& hub, uint8_t port);

}  // namespace usb::hub
