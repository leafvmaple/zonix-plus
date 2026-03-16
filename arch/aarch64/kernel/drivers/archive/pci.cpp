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

    // Sanity: read device 0 vendor ID
    uint32_t id0 = *reinterpret_cast<volatile uint32_t*>(ecam_base);
    if (id0 == 0xFFFFFFFF || (id0 & 0xFFFF) == 0xFFFF) {
        cprintf("pci: no devices on bus 0\n");
        return -1;
    }
    cprintf("pci: ECAM at 0x%lx, host bridge %04lx:%04lx\n", static_cast<unsigned long>(va),
            static_cast<unsigned long>(id0 & 0xFFFF), static_cast<unsigned long>(id0 >> 16));
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

// Simple linear BAR allocator for MMIO32 window
static uintptr_t mmio32_next = PCI_MMIO32_BASE;

static uintptr_t alloc_mmio32(size_t size) {
    // Align to size (BAR sizes are power-of-2)
    uintptr_t aligned = (mmio32_next + size - 1) & ~(size - 1);
    if (aligned + size > PCI_MMIO32_END)
        return 0;
    mmio32_next = aligned + size;
    return aligned;
}

void assign_bars() {
    for (int dev = 0; dev < 32; dev++) {
        uint32_t id = config_read32(0, dev, 0, VENDOR_ID);
        if (id == 0xFFFFFFFF || (id & 0xFFFF) == 0xFFFF)
            continue;

        // Disable MMIO + bus-master during BAR programming
        uint32_t cmd = config_read32(0, dev, 0, COMMAND);
        config_write32(0, dev, 0, COMMAND, cmd & ~(CMD_MEMORY_SPACE | CMD_BUS_MASTER));

        for (int bar = 0; bar < 6; bar++) {
            int reg = BAR0 + bar * 4;
            uint32_t orig = config_read32(0, dev, 0, reg);

            // Write all 1s to determine size
            config_write32(0, dev, 0, reg, 0xFFFFFFFF);
            uint32_t mask = config_read32(0, dev, 0, reg);
            config_write32(0, dev, 0, reg, orig);  // restore

            if (mask == 0 || mask == 0xFFFFFFFF)
                continue;

            bool is_io = (mask & 1);
            if (is_io)
                continue;  // skip I/O BARs

            bool is_64 = ((mask >> 1) & 3) == 2;
            uint32_t size_mask = mask & 0xFFFFFFF0;
            uint32_t size = (~size_mask) + 1;

            uintptr_t addr = alloc_mmio32(size);
            if (addr == 0) {
                cprintf("pci: MMIO32 exhausted for dev %d BAR%d\n", dev, bar);
                continue;
            }

            config_write32(0, dev, 0, reg, static_cast<uint32_t>(addr));
            if (is_64) {
                config_write32(0, dev, 0, reg + 4, 0);  // high 32 bits = 0
                bar++;                                  // skip next BAR (consumed by 64-bit)
            }

            cprintf("pci: dev %d BAR%d = 0x%lx (size 0x%lx%s)\n", dev, bar - (is_64 ? 1 : 0),
                    static_cast<unsigned long>(addr), static_cast<unsigned long>(size), is_64 ? ", 64-bit" : "");
        }

        // Re-enable MMIO + bus-master
        config_write32(0, dev, 0, COMMAND, cmd | CMD_MEMORY_SPACE | CMD_BUS_MASTER);
    }
}

bool find_by_class(uint8_t cls, uint8_t sub, uint8_t iface, int* out_bus, int* out_dev, int* out_func) {
    uint32_t expected = (static_cast<uint32_t>(cls) << 16) | (static_cast<uint32_t>(sub) << 8) | iface;
    for (int bus = 0; bus < 1; bus++) {  // QEMU virt: bus 0 only
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = config_read32(bus, dev, func, VENDOR_ID);
                if (id == 0xFFFFFFFF || (id & 0xFFFF) == 0xFFFF)
                    continue;
                uint32_t cr = config_read32(bus, dev, func, CLASS_REVISION);
                if ((cr >> 8) == expected) {
                    *out_bus = bus;
                    *out_dev = dev;
                    *out_func = func;
                    return true;
                }
            }
        }
    }
    return false;
}

bool find_by_id(uint16_t vendor, uint16_t device, int* out_bus, int* out_dev, int* out_func) {
    uint32_t expected = (static_cast<uint32_t>(device) << 16) | vendor;
    for (int bus = 0; bus < 1; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = config_read32(bus, dev, func, VENDOR_ID);
                if (id == expected) {
                    *out_bus = bus;
                    *out_dev = dev;
                    *out_func = func;
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace pci
