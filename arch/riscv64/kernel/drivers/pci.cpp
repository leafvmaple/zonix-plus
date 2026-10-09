/**
 * @file pci.cpp
 * @brief RISC-V PCI ECAM configuration space access.
 *
 * PCI ECAM window physical address is board-specific.
 */

#include "drivers/pci.h"
#include "drivers/mmio.h"
#include "lib/stdio.h"
#include "mm/vmm.h"
#include <asm/page.h>
#include <asm/board.h>

static constexpr uintptr_t PCI_ECAM_PHYS = BOARD_PCI_ECAM_PHYS;
static constexpr size_t PCI_ECAM_SIZE = BOARD_PCI_ECAM_SIZE;

namespace {

volatile uint8_t* ecam_base = nullptr;

static uintptr_t ecam_offset(int bus_number, int device_number, int function_number, int offset_bytes) {
    return (static_cast<uintptr_t>(bus_number) << 20) | (static_cast<uintptr_t>(device_number) << 15) |
           (static_cast<uintptr_t>(function_number) << 12) | (offset_bytes & 0xFFC);
}

}  // namespace

namespace pci {

int init() {
    if (PCI_ECAM_PHYS == 0 || PCI_ECAM_SIZE == 0) {
        cprintf("pci: no ECAM configured for this board\n");
        return 0;
    }
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

uint32_t config_read32(int bus_number, int device_number, int function_number, int offset_bytes) {
    if (!ecam_base) {
        return 0xFFFFFFFF;
    }
    return mmio::read32(ecam_base, ecam_offset(bus_number, device_number, function_number, offset_bytes));
}

void config_write32(int bus_number, int device_number, int function_number, int offset_bytes, uint32_t value) {
    if (!ecam_base) {
        return;
    }
    mmio::write32(ecam_base, ecam_offset(bus_number, device_number, function_number, offset_bytes), value);
}

int bus_count() {
    return 1;
}

}  // namespace pci
