#pragma once

#include <base/types.h>

struct PCILocation {
    uint8_t bus{};
    uint8_t device{};
    uint8_t function{};

    PCILocation() = default;
    PCILocation(uint8_t b, uint8_t d, uint8_t f) : bus(b), device(d), function(f) {}

    constexpr uint32_t get_address() const {
        return (1U << 31) |
               (static_cast<uint32_t>(bus) << 16) |
               (static_cast<uint32_t>(device) << 11) |
               (static_cast<uint32_t>(function) << 8);
    }

    uint32_t read_config32(uint8_t offset);
    void write_config32(uint8_t offset, uint32_t value);

    uint32_t read_bar(uint8_t bar_index);
    void enable_bus_master();

    static bool find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t interface, PCILocation* loc);
};

namespace pci {

// PCI configuration space register offsets
enum PCIConfigOffset : uint8_t {
    PCI_VENDOR_ID       = 0x00,  // Vendor ID (16-bit) + Device ID (16-bit)
    PCI_COMMAND         = 0x04,  // Command + Status
    PCI_CLASS_REVISION  = 0x08,  // Revision ID + Class Code
    PCI_HEADER_TYPE     = 0x0C,  // BIST, Header Type, Latency Timer, Cache Line Size
    PCI_BAR0            = 0x10,
    PCI_BAR1            = 0x14,
    PCI_BAR2            = 0x18,
    PCI_BAR3            = 0x1C,
    PCI_BAR4            = 0x20,
    PCI_BAR5            = 0x24,
    PCI_INTERRUPT       = 0x3C,  // Interrupt Line + Interrupt Pin
};

// PCI configuration space ports
constexpr uint16_t CONFIG_ADDRESS = 0xCF8;
constexpr uint16_t CONFIG_DATA = 0xCFC;

// PCI class codes
constexpr uint32_t CLASS_MASS_STORAGE = 0x01;
constexpr uint32_t SUBCLASS_SATA = 0x06;
constexpr uint32_t INTERFACE_AHCI = 0x01;

// PCI command register bits
constexpr uint16_t CMD_IO_SPACE = 0x01;
constexpr uint16_t CMD_MEMORY_SPACE = 0x02;
constexpr uint16_t CMD_BUS_MASTER = 0x04;

} // namespace pci
