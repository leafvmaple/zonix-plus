#include "drivers/pci.h"
#include "lib/stdio.h"
#include "mm/vmm.h"
#include <asm/page.h>

// QEMU virt machine ECAM PCI configuration base (from DTB).
// Bus 0 only — sufficient for QEMU virt topology.
static constexpr uintptr_t PCI_ECAM_PHYS = 0x4010000000ULL;
static constexpr size_t PCI_ECAM_SIZE = 0x00100000;  // 1 MB (bus 0)

namespace {

volatile uint8_t* ecam_base = nullptr;

volatile uint32_t* ecam_addr(int bus, int dev, int func, int offset) {
    uintptr_t off = (static_cast<uintptr_t>(bus) << 20) | (static_cast<uintptr_t>(dev) << 15) |
                    (static_cast<uintptr_t>(func) << 12) | (offset & 0xFFC);
    return reinterpret_cast<volatile uint32_t*>(ecam_base + off);
}

}  // namespace

namespace pci {

int init() {
    uintptr_t va = vmm::mmio_map(PCI_ECAM_PHYS, PCI_ECAM_SIZE, VM_WRITE | VM_NOCACHE);
    if (va == 0) {
        cprintf("pci: failed to map ECAM\n");
        return -1;
    }
    ecam_base = reinterpret_cast<volatile uint8_t*>(va);

    uint32_t id0 = *reinterpret_cast<volatile uint32_t*>(ecam_base);
    if (id0 == 0xFFFFFFFF || (id0 & 0xFFFF) == 0xFFFF) {
        cprintf("pci: no devices on bus 0\n");
        return -1;
    }
    cprintf("pci: ECAM mapped, host bridge %04x:%04x\n", static_cast<unsigned>(id0 & 0xFFFF),
            static_cast<unsigned>(id0 >> 16));
    return 0;
}

uint32_t config_read32(int bus, int dev, int func, int offset) {
    return *ecam_addr(bus, dev, func, offset);
}

void config_write32(int bus, int dev, int func, int offset, uint32_t val) {
    *ecam_addr(bus, dev, func, offset) = val;
}

int bus_count() {
    return 1;
}  // ECAM maps bus 0 only

}  // namespace pci
