#include "drivers/pci_ecam.h"
#include <kernel/log.h>
#include "drivers/mmio.h"
#include "drivers/pci.h"
#include "lib/stdio.h"
#include <kernel/vm_flags.h>

namespace pci {

Error EcamConfig::init(uintptr_t pa, size_t byte_count) {
    constexpr size_t bus_bytes = 1U << 20;
    ENSURE(!mapping_, Error::Busy);
    ENSURE(pa != 0 && byte_count != 0, Error::NoDevice);
    ENSURE((pa & (bus_bytes - 1)) == 0 && byte_count % bus_bytes == 0 && byte_count / bus_bytes <= 256);
    mapping_ = TRY(vmm::map_mmio(pa, byte_count, VM_WRITE | VM_NOCACHE));
    bus_count_ = static_cast<int>(byte_count / bus_bytes);
    uint32_t id = read32(0, 0, 0, VendorId);
    if (id != 0xFFFFFFFF && (id & 0xFFFF) != 0xFFFF) {
        klog::debug("pci: ECAM mapped, host bridge %04x:%04x\n", static_cast<unsigned>(id & 0xFFFF),
                    static_cast<unsigned>(id >> 16));
    }
    return Error::None;
}

bool EcamConfig::valid_address(int bus, int slot, int function, int offset) {
    return mapping_ && bus >= 0 && bus < bus_count_ && slot >= 0 && slot < 32 && function >= 0 && function < 8 &&
           offset >= 0 && offset <= 4092 && (offset & 3) == 0;
}

uintptr_t EcamConfig::address(int bus, int slot, int function, int offset) {
    return mapping_->address() + (static_cast<uintptr_t>(bus) << 20) + (static_cast<uintptr_t>(slot) << 15) +
           (static_cast<uintptr_t>(function) << 12) + static_cast<uintptr_t>(offset);
}

uint32_t EcamConfig::read32(int bus, int slot, int function, int offset) {
    return valid_address(bus, slot, function, offset) ? mmio::read32(address(bus, slot, function, offset)) : 0xFFFFFFFF;
}

void EcamConfig::write32(int bus, int slot, int function, int offset, uint32_t value) {
    if (valid_address(bus, slot, function, offset)) {
        mmio::write32(address(bus, slot, function, offset), value);
    }
}

}  // namespace pci
