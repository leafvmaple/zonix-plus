#pragma once

#include <base/types.h>

// QEMU virt machine ECAM PCI configuration base (from DTB reg property).
// The "high" ECAM sits at physical 0x40_1000_0000 on QEMU virt.
// Bus 0 only — sufficient for QEMU virt topology.
constexpr uintptr_t PCI_ECAM_PHYS = 0x4010000000ULL;
constexpr size_t PCI_ECAM_SIZE = 0x00100000;  // 1 MB (bus 0, 32 devs × 8 funcs)

namespace pci {

// PCI configuration space register offsets
enum ConfigOffset : uint8_t {
    VENDOR_ID = 0x00,
    COMMAND = 0x04,
    CLASS_REVISION = 0x08,
    BAR0 = 0x10,
    BAR1 = 0x14,
    CAP_PTR = 0x34,
};

// PCI command register bits
constexpr uint16_t CMD_IO_SPACE = 0x01;
constexpr uint16_t CMD_MEMORY_SPACE = 0x02;
constexpr uint16_t CMD_BUS_MASTER = 0x04;

// QEMU virt machine PCI MMIO32 window (from DTB ranges property)
constexpr uintptr_t PCI_MMIO32_BASE = 0x10000000ULL;
constexpr uintptr_t PCI_MMIO32_END = 0x3EFFFFFFULL;

// Initialise ECAM mapping (call after VMM is up)
int init();

// Enumerate bus 0 and assign BARs for all devices
void assign_bars();

// Read/write 32-bit config register at (bus, dev, func, offset)
uint32_t config_read32(int bus, int dev, int func, int offset);
void config_write32(int bus, int dev, int func, int offset, uint32_t val);

// Read a BAR value
uint32_t read_bar(int bus, int dev, int func, int bar_index);

// Enable bus-master + memory-space for a device
void enable_bus_master(int bus, int dev, int func);

// Find first device matching class/subclass/interface.
// Returns true on success, filling bus/dev/func.
bool find_by_class(uint8_t cls, uint8_t sub, uint8_t iface, int* out_bus, int* out_dev, int* out_func);

// Find first device by vendor/device ID pair.
bool find_by_id(uint16_t vendor, uint16_t device, int* out_bus, int* out_dev, int* out_func);

}  // namespace pci
