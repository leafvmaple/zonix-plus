#pragma once

// JH7110 PCIe0 root port (PLDA XpressRICH3), which carries the VisionFive
// 2's VIA VL805 USB 3 controller. Firmware leaves it in reset.

#include "lib/result.h"

namespace jh7110_pcie {

inline constexpr uintptr_t CONFIG_PHYS = 0x940000000ULL;
inline constexpr int BUSES = 2;  // The root port and the device behind it

// Starts the controller and trains the link; the configuration window at
// CONFIG_PHYS then reaches bus 0 (the root port) and bus 1.
Error start();
// Gives the device on bus 1 its memory BARs and opens the bridge to them.
Error assign_resources();
// Whether configuration space at bus/slot/function exists, and whether a
// register reads as zero and ignores writes (the root port's own BARs).
bool present(int bus, int slot, int function);
bool hidden(int bus, int offset);

}  // namespace jh7110_pcie
