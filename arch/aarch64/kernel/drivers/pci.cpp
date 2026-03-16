#include "pci.h"
#include "lib/stdio.h"
#include "mm/vmm.h"
#include <asm/page.h>

namespace {

// Kernel virtual address of the ECAM window (set by pci::init)
volatile uint8_t* ecam_base = nullptr;

// ECAM address for a given BDF + register offset
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

    // Sanity: read device 0 vendor ID (should be the host bridge)
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

uint32_t read_bar(int bus, int dev, int func, int bar_index) {
    return config_read32(bus, dev, func, pci::BAR0 + bar_index * 4);
}

void enable_bus_master(int bus, int dev, int func) {
    uint32_t cmd = config_read32(bus, dev, func, pci::COMMAND);
    cmd |= CMD_BUS_MASTER | CMD_MEMORY_SPACE;
    config_write32(bus, dev, func, pci::COMMAND, cmd);
}

bool find_by_class(uint8_t cls, uint8_t sub, uint8_t iface, int* out_bus, int* out_dev, int* out_func) {
    uint32_t expected = (static_cast<uint32_t>(cls) << 16) | (static_cast<uint32_t>(sub) << 8) | iface;
    for (int dev = 0; dev < 32; dev++) {
        for (int func = 0; func < 8; func++) {
            uint32_t id = config_read32(0, dev, func, VENDOR_ID);
            if (id == 0xFFFFFFFF || (id & 0xFFFF) == 0xFFFF)
                continue;
            uint32_t cr = config_read32(0, dev, func, CLASS_REVISION);
            if ((cr >> 8) == expected) {
                *out_bus = 0;
                *out_dev = dev;
                *out_func = func;
                return true;
            }
        }
    }
    return false;
}

bool find_by_id(uint16_t vendor, uint16_t device, int* out_bus, int* out_dev, int* out_func) {
    uint32_t expected = static_cast<uint32_t>(vendor) | (static_cast<uint32_t>(device) << 16);
    for (int dev = 0; dev < 32; dev++) {
        for (int func = 0; func < 8; func++) {
            uint32_t id = config_read32(0, dev, func, VENDOR_ID);
            if (id == expected) {
                *out_bus = 0;
                *out_dev = dev;
                *out_func = func;
                return true;
            }
        }
    }
    return false;
}

uint8_t read_interrupt_line(int bus, int dev, int func) {
    return static_cast<uint8_t>(config_read32(bus, dev, func, 0x3C) & 0xFF);
}

}  // namespace pci
