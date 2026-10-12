#pragma once

#include "mm/mmio.h"

namespace pci {

// Owns the mapped configuration window; board addresses remain in arch/.
class EcamConfig {
public:
    static Error init(uintptr_t pa, size_t byte_count);
    static uint32_t read32(int bus, int slot, int function, int offset);
    static void write32(int bus, int slot, int function, int offset, uint32_t value);
    static int bus_count() { return bus_count_; }

private:
    static bool valid_address(int bus, int slot, int function, int offset);
    static uintptr_t address(int bus, int slot, int function, int offset);
    inline static vmm::MmioRegion mapping_{};
    inline static int bus_count_{};
};

}  // namespace pci
