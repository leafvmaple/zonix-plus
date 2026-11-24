#pragma once

// Innosilicon HDMI transmitter with its integrated PHY, as on the JH7110.
// Registers are bytes, each in its own 32-bit word. Bring-up runs in steps
// so the caller can wait between them: configure the PLLs, wait until
// `plls_locked`, then `enable` the TMDS output.

#include <base/types.h>
#include "drivers/display/display_timing.h"
#include "lib/result.h"

namespace inno_hdmi {

// Only 74.25 and 148.5 MHz pixel clocks are supported.
Error configure_plls(uintptr_t base, const DisplayTiming& timing);
bool plls_locked(uintptr_t base);
void enable(uintptr_t base, const DisplayTiming& timing);
void disable(uintptr_t base);

}  // namespace inno_hdmi
