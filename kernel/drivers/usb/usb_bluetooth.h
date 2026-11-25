#pragma once

// USB Bluetooth adapters (wireless controller class, Bluetooth programming
// interface): HCI commands go out as class requests on endpoint 0 and events
// arrive on the interrupt IN endpoint; the HCI layer does the rest.

#include "drivers/usb/usb.h"

namespace usb::bluetooth {

// Claims a Bluetooth HCI interface; NotSupported for others.
Error attach(Device& device, const InterfaceDescriptor& interface, const EndpointDescriptor* endpoints,
             size_t endpoint_count);
// Forgets a departing device's adapter.
void detach(Device& device);

}  // namespace usb::bluetooth
