/**
 * @file pci.cpp
 * @brief RISC-V QEMU virt PCI ECAM configuration space access.
 *
 * QEMU virt RISC-V maps the PCI ECAM window at physical 0x30000000.
 * Segment 0, buses 0-63, at 2MB per bus = bus 0 base + offset.
 */

#include "drivers/pci.h"
#include "drivers/mmio.h"
#include "lib/stdio.h"
#include "mm/vmm.h"
#include <asm/page.h>

/* QEMU virt RISC-V PCI ECAM physical base */
static constexpr uintptr_t PCI_ECAM_PHYS = 0x30000000UL;
static constexpr size_t PCI_ECAM_SIZE = 0x00100000; /* 1 MB (bus 0) */

namespace {

volatile uint8_t* ecam_base = nullptr;

static uintptr_t ecam_offset(int bus, int dev, int func, int offset) {
    return (static_cast<uintptr_t>(bus) << 20) | (static_cast<uintptr_t>(dev) << 15) |
           (static_cast<uintptr_t>(func) << 12) | (offset & 0xFFC);
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

    uint32_t id0 = mmio::read32(ecam_base, 0);
    if (id0 == 0xFFFFFFFF || (id0 & 0xFFFF) == 0xFFFF) {
        cprintf("pci: no devices on bus 0\n");
        return 0; /* not fatal — some QEMU configs may not have PCI */
    }
    cprintf("pci: ECAM mapped, host bridge %04x:%04x\n", static_cast<unsigned>(id0 & 0xFFFF),
            static_cast<unsigned>(id0 >> 16));
    return 0;
}

uint32_t config_read32(int bus, int dev, int func, int offset) {
    if (!ecam_base) {
        return 0xFFFFFFFF;
    }
    return mmio::read32(ecam_base, ecam_offset(bus, dev, func, offset));
}

void config_write32(int bus, int dev, int func, int offset, uint32_t val) {
    if (!ecam_base) {
        return;
    }
    mmio::write32(ecam_base, ecam_offset(bus, dev, func, offset), val);
}

int bus_count() {
    return 1;
}

}  // namespace pci
