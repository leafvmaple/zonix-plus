#pragma once

#include <base/types.h>
#include "lib/result.h"

namespace pci {

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

inline constexpr uint16_t CMD_IO_SPACE = 0x0001;
inline constexpr uint16_t CMD_MEMORY_SPACE = 0x0002;
inline constexpr uint16_t CMD_BUS_MASTER = 0x0004;
inline constexpr uint16_t CMD_INTX_DISABLE = 0x0400;

inline constexpr uint8_t CLASS_MASS_STORAGE = 0x01;
inline constexpr uint8_t SUBCLASS_SATA = 0x06;
inline constexpr uint8_t INTERFACE_AHCI = 0x01;

inline constexpr uint8_t CLASS_SYSTEM_PERIPHERAL = 0x08;
inline constexpr uint8_t SUBCLASS_SD_HOST = 0x05;
inline constexpr uint8_t INTERFACE_SDHCI = 0x00;
inline constexpr uint8_t INTERFACE_SDHCI_DMA = 0x01;

int init();

uint32_t config_read32(int bus, int dev, int func, int offset);
void config_write32(int bus, int dev, int func, int offset, uint32_t val);
int bus_count();

struct DeviceInfo {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t cls;
    uint8_t subcls;
    uint8_t iface;
    uint8_t header_type;
};

inline constexpr uint16_t ANY_ID = 0xFFFF;
inline constexpr uint8_t ANY_CLASS = 0xFF;

struct DriverId {
    uint16_t vendor;
    uint16_t device;
    uint8_t cls;
    uint8_t subcls;
    uint8_t iface;
};

using ProbeFn = Error (*)(const DeviceInfo* dev, const DriverId* id);

struct Driver {
    const char* name;
    const DriverId* id_table;
    int id_count;
    ProbeFn probe;
};

Error register_driver(const Driver* driver);
int probe_drivers();

uint32_t read_bar(int bus, int dev, int func, int bar_index);
void enable_bus_master(int bus, int dev, int func);

}  // namespace pci
