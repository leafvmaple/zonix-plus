#pragma once

#include <base/types.h>

namespace pci {

// ---- PCI configuration space register offsets ----
enum ConfigOffset : uint8_t {
    VENDOR_ID = 0x00,       // Vendor ID (16) + Device ID (16)
    COMMAND = 0x04,         // Command (16) + Status (16)
    CLASS_REVISION = 0x08,  // Revision ID (8) + Class Code (24)
    HEADER_TYPE = 0x0C,     // BIST, Header Type, Latency Timer, Cache Line
    BAR0 = 0x10,
    BAR1 = 0x14,
    BAR2 = 0x18,
    BAR3 = 0x1C,
    BAR4 = 0x20,
    BAR5 = 0x24,
    CAP_PTR = 0x34,    // Capabilities pointer
    INTERRUPT = 0x3C,  // Interrupt Line + Interrupt Pin
};

// ---- PCI command register bits ----
constexpr uint16_t CMD_IO_SPACE = 0x0001;
constexpr uint16_t CMD_MEMORY_SPACE = 0x0002;
constexpr uint16_t CMD_BUS_MASTER = 0x0004;
constexpr uint16_t CMD_INTX_DISABLE = 0x0400;

// ---- Common PCI class codes ----
constexpr uint8_t CLASS_MASS_STORAGE = 0x01;
constexpr uint8_t SUBCLASS_SATA = 0x06;
constexpr uint8_t INTERFACE_AHCI = 0x01;

// ================================================================
// Arch-specific functions (implemented per platform)
// ================================================================

// Platform init (e.g. aarch64 maps ECAM; x86 can be a no-op).
// Must be called before any other pci:: function.
int init();

// Raw 32-bit config-space read/write.
uint32_t config_read32(int bus, int dev, int func, int offset);
void config_write32(int bus, int dev, int func, int offset, uint32_t val);

// Number of PCI buses the platform can enumerate.
int bus_count();

// ================================================================
// Generic functions (shared implementation in kernel/drivers/pci.cpp)
// ================================================================

uint32_t read_bar(int bus, int dev, int func, int bar_index);
void enable_bus_master(int bus, int dev, int func);

bool find_by_class(uint8_t cls, uint8_t sub, uint8_t iface, int* out_bus, int* out_dev, int* out_func);
bool find_by_id(uint16_t vendor, uint16_t device, int* out_bus, int* out_dev, int* out_func);

uint8_t read_interrupt_line(int bus, int dev, int func);

}  // namespace pci
