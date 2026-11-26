#pragma once

#include <base/types.h>
#include "lib/result.h"

// Synopsys DesignWare Ethernet QoS controller (GMAC 5.x) with one DMA
// channel, polled on the timer tick, offered as /dev/net0 like the VirtIO
// network device. Its PHY must already be configured by firmware for the
// board's interface; the driver reads the link the PHY negotiated.
namespace dwc_eqos {

// Offers the controller whose registers `base` maps as /dev/net0. Its clocks
// and resets must already run. The hardware is first touched when the device
// is opened; a second controller returns Busy.
Error attach(uintptr_t base);
// Called from the timer interrupt.
void tick();

}  // namespace dwc_eqos
