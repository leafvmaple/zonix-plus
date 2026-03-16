#include "drivers/pci.h"

namespace pci {

uint32_t read_bar(int bus, int dev, int func, int bar_index) {
    if (bar_index < 0 || bar_index > 5)
        return 0;
    return config_read32(bus, dev, func, BAR0 + bar_index * 4);
}

void enable_bus_master(int bus, int dev, int func) {
    uint32_t cmd = config_read32(bus, dev, func, COMMAND);
    cmd |= CMD_BUS_MASTER | CMD_MEMORY_SPACE;
    config_write32(bus, dev, func, COMMAND, cmd);
}

bool find_by_class(uint8_t cls, uint8_t sub, uint8_t iface, int* out_bus, int* out_dev, int* out_func) {
    uint32_t expected = (static_cast<uint32_t>(cls) << 16) | (static_cast<uint32_t>(sub) << 8) | iface;
    int buses = bus_count();
    for (int bus = 0; bus < buses; bus++)
        for (int dev = 0; dev < 32; dev++)
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
    return false;
}

bool find_by_id(uint16_t vendor, uint16_t device, int* out_bus, int* out_dev, int* out_func) {
    uint32_t expected = static_cast<uint32_t>(vendor) | (static_cast<uint32_t>(device) << 16);
    int buses = bus_count();
    for (int bus = 0; bus < buses; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int func = 0; func < 8; func++) {
                uint32_t id = config_read32(bus, dev, func, VENDOR_ID);
                if (id == expected) {
                    *out_bus = bus;
                    *out_dev = dev;
                    *out_func = func;
                    return true;
                }
            }
    return false;
}

uint8_t read_interrupt_line(int bus, int dev, int func) {
    return static_cast<uint8_t>(config_read32(bus, dev, func, INTERRUPT) & 0xFF);
}

}  // namespace pci
