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

    uint32_t read_bar(uint8_t barIndex);
    void enable_bus_master();

    static bool find_device_by_class(uint8_t classCode, uint8_t subclass, uint8_t interface, PCILocation* loc);
};

namespace pci {

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
