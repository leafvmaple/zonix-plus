#pragma once

// HID boot-protocol keyboards and mice. Reports become the same key and
// pointer events as other input devices, so the console and Desktop use
// them unchanged.

#include "drivers/usb/usb.h"

namespace usb::hid {

// Claims a boot keyboard or mouse interface; NotSupported for others.
Error attach(Device& device, const InterfaceDescriptor& interface, const EndpointDescriptor* endpoints,
             size_t endpoint_count);
// Timer tick: repeats a held key, as USB keyboards do not.
void tick();

// Boot keyboard usage IDs to the key codes of the display ABI; 0 = none.
uint16_t key_code(uint8_t usage);

}  // namespace usb::hid
