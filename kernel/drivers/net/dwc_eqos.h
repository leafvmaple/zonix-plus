#pragma once

#include <base/types.h>
#include "lib/result.h"

// Synopsys DesignWare Ethernet QoS controller (GMAC 5.x) with one DMA
// channel, polled on the timer tick, offered as /dev/net0 like the VirtIO
// network device. Its PHY must already be configured by firmware for the
// board's interface; the driver reads the link the PHY negotiated.
namespace dwc_eqos {

constexpr size_t MAC_BYTES = 6;

// Offers the controller whose registers `base` maps as /dev/net0. Its clocks
// and resets must already run. The hardware is first touched when the device
// is opened; a second controller returns Busy. `mac` is the board's address
// from firmware, or null to use the one firmware left in the controller.
Error attach(uintptr_t base, const uint8_t* mac);
// Called from the timer interrupt.
void tick();

}  // namespace dwc_eqos
