/**
 * @file pci.cpp
 * @brief x86_64 PCI config-space transport — legacy port I/O (CF8h/CFCh).
 *
 * Provides the three arch-specific primitives declared in <drivers/pci.h>:
 *   pci::init(), pci::config_read32(), pci::config_write32(), pci::bus_count()
 * Generic PCI functions live in kernel/drivers/pci.cpp.
 */

#include "drivers/pci.h"
#include <asm/arch.h>
#include "lib/stdio.h"

static constexpr uint16_t CONFIG_ADDRESS = 0xCF8;
static constexpr uint16_t CONFIG_DATA = 0xCFC;

static uint32_t make_address(int bus, int dev, int func, int offset) {
    return (1U << 31) | (static_cast<uint32_t>(bus & 0xFF) << 16) | (static_cast<uint32_t>(dev & 0x1F) << 11) |
           (static_cast<uint32_t>(func & 0x07) << 8) | (offset & 0xFC);
}

namespace pci {

int init() {
    /* Port I/O is always available on x86 — nothing to set up */
    return 0;
}

uint32_t config_read32(int bus, int dev, int func, int offset) {
    arch_port_outl(CONFIG_ADDRESS, make_address(bus, dev, func, offset));
    return arch_port_inl(CONFIG_DATA);
}

void config_write32(int bus, int dev, int func, int offset, uint32_t val) {
    arch_port_outl(CONFIG_ADDRESS, make_address(bus, dev, func, offset));
    arch_port_outl(CONFIG_DATA, val);
}

int bus_count() {
    return 256;
}

}  // namespace pci
