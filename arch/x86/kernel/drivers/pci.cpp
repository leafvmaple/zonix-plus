#include "drivers/pci.h"
#include <asm/arch.h>
#include "lib/stdio.h"

static constexpr uint16_t CONFIG_ADDRESS = 0xCF8;
static constexpr uint16_t CONFIG_DATA = 0xCFC;

static uint32_t make_address(int bus_number, int device_number, int function_number, int offset_bytes) {
    return (1U << 31) | (static_cast<uint32_t>(bus_number & 0xFF) << 16) |
           (static_cast<uint32_t>(device_number & 0x1F) << 11) | (static_cast<uint32_t>(function_number & 0x07) << 8) |
           (offset_bytes & 0xFC);
}

namespace pci {

int init() {
    /* Port I/O is always available on x86 — nothing to set up */
    return 0;
}

uint32_t config_read32(int bus_number, int device_number, int function_number, int offset_bytes) {
    arch_port_write32(CONFIG_ADDRESS, make_address(bus_number, device_number, function_number, offset_bytes));
    return arch_port_read32(CONFIG_DATA);
}

void config_write32(int bus_number, int device_number, int function_number, int offset_bytes, uint32_t value) {
    arch_port_write32(CONFIG_ADDRESS, make_address(bus_number, device_number, function_number, offset_bytes));
    arch_port_write32(CONFIG_DATA, value);
}

int bus_count() {
    return 256;
}

}  // namespace pci
