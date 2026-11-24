#pragma once

// USB 2.0 hubs: powers and resets each downstream port and enumerates the
// devices found there. Devices present when the hub attaches are found;
// later insertions are not yet watched.

#include "drivers/usb/usb.h"

namespace usb::hub {

// Claims a hub interface; NotSupported for others.
Error attach(Device& device, const InterfaceDescriptor& interface);

}  // namespace usb::hub
