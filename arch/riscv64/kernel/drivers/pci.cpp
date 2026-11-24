#include "drivers/pci.h"
#include "drivers/pci_ecam.h"
#include "jh7110_pcie.h"
#include <asm/board.h>

// QEMU virt exposes a firmware-configured ECAM window. The VisionFive 2's
// firmware leaves PCIe off: the kernel starts the JH7110 root port, whose
// configuration window is ECAM-shaped but reaches only one device per bus
// and must not expose the root port's own BARs.

namespace pci {

int init() {
    if constexpr (BOARD_JH7110_PCIE) {
        if (jh7110_pcie::start() != Error::None)
            return 0;  // No link: the board runs without PCIe devices.
        if (EcamConfig::init(jh7110_pcie::CONFIG_PHYS, jh7110_pcie::BUSES << 20) != Error::None)
            return -1;
        return jh7110_pcie::assign_resources() == Error::None ? 0 : -1;
    }
    Error error = EcamConfig::init(BOARD_PCI_ECAM_PHYS, BOARD_PCI_ECAM_SIZE);
    return error == Error::None || error == Error::NoDevice ? 0 : -1;
}

uint32_t config_read32(int bus, int slot, int function, int offset) {
    if constexpr (BOARD_JH7110_PCIE) {
        if (!jh7110_pcie::present(bus, slot, function))
            return 0xFFFFFFFF;
        if (jh7110_pcie::hidden(bus, offset))
            return 0;
    }
    return EcamConfig::read32(bus, slot, function, offset);
}

void config_write32(int bus, int slot, int function, int offset, uint32_t value) {
    if constexpr (BOARD_JH7110_PCIE) {
        if (!jh7110_pcie::present(bus, slot, function) || jh7110_pcie::hidden(bus, offset))
            return;
    }
    EcamConfig::write32(bus, slot, function, offset, value);
}

int bus_count() {
    return EcamConfig::bus_count();
}

}  // namespace pci
