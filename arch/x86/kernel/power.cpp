#include <kernel/power.h>
#include <asm/arch.h>

#include "drivers/intr.h"
#include "drivers/pci.h"

namespace {

// The configured QEMU pc machine exposes a PIIX4 power-management function.
constexpr uint32_t PIIX4_PM_PCI_ID = 0x71138086;
constexpr int PM_BASE_OFFSET = 0x40;
constexpr int PM_ENABLE_OFFSET = 0x80;
constexpr uint32_t PM_BASE_MASK = 0xFFC0;
constexpr uint32_t PM_IO_ENABLE = 0x01;
constexpr uint16_t PM1_CONTROL_OFFSET = 0x04;
constexpr uint16_t PM1_SLEEP_TYPE_MASK = 7U << 10;
constexpr uint16_t PM1_SLEEP_ENABLE = 1U << 13;

}  // namespace

Error power::shutdown() {
    intr::Guard guard;
    for (int slot = 0; slot < 32; ++slot) {
        for (int function = 0; function < 8; ++function) {
            if (pci::config_read32(0, slot, function, pci::VendorId) != PIIX4_PM_PCI_ID)
                continue;
            if ((pci::config_read32(0, slot, function, PM_ENABLE_OFFSET) & PM_IO_ENABLE) == 0)
                return Error::NotSupported;
            const uint32_t base = pci::config_read32(0, slot, function, PM_BASE_OFFSET) & PM_BASE_MASK;
            if (base == 0)
                return Error::NoDevice;
            const uint16_t port = static_cast<uint16_t>(base + PM1_CONTROL_OFFSET);
            const uint16_t control = arch_port_read16(port);
            // QEMU's PIIX4 S5 sleep type is zero; retain the other control bits.
            arch_port_write16(port, (control & ~PM1_SLEEP_TYPE_MASK) | PM1_SLEEP_ENABLE);
            arch_halt_forever();
        }
    }
    return Error::NotSupported;
}
