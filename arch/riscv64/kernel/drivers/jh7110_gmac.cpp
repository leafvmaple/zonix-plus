// JH7110 GMAC1: the DesignWare Ethernet QoS controller on the VisionFive 2's
// Ethernet port that U-Boot uses. Firmware leaves it configured for the
// board (RGMII, PHY delays, transmit clock from the PHY); the kernel makes
// sure its bus clocks and resets are on and hands it to the generic driver.

#include "jh7110_gmac.h"
#include "drivers/device_tree.h"
#include "drivers/mmio.h"
#include "drivers/net/dwc_eqos.h"
#include <kernel/log.h>

#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>

namespace {

// System clock and reset generator: one register per clock with its gate in
// the top bit, then the reset assert bitmaps.
constexpr uint32_t CLOCK_GATE_ENABLE = 1U << 31;
constexpr uintptr_t RESET_ASSERT_OFFSET = 0x2F8;

[[maybe_unused]] void enable_bus() {
    constexpr uint32_t clocks[] = {BOARD_GMAC_AHB_CLOCK, BOARD_GMAC_AXI_CLOCK};
    constexpr uint32_t resets[] = {BOARD_GMAC_AXI_RESET, BOARD_GMAC_AHB_RESET};
    const uintptr_t crg = BOARD_SYSCRG_PHYS + KERNEL_BASE;
    for (uint32_t clock : clocks) {
        const uintptr_t offset = static_cast<uintptr_t>(clock) * sizeof(uint32_t);
        mmio::write32(crg, offset, mmio::read32(crg, offset) | CLOCK_GATE_ENABLE);
    }
    for (uint32_t reset : resets) {
        const uintptr_t offset = RESET_ASSERT_OFFSET + reset / 32 * sizeof(uint32_t);
        mmio::write32(crg, offset, mmio::read32(crg, offset) & ~(1U << (reset % 32)));
    }
    arch_mb();
}

// U-Boot writes the address it read from the board's EEPROM into the device
// tree it passes on; the controller's own address register is cleared when
// U-Boot stops the port before starting the OS.
[[maybe_unused]] const uint8_t* board_address() {
    auto tree = device_tree::firmware();
    if (!tree.ok())
        return nullptr;
    constexpr const char* names[] = {"local-mac-address", "mac-address"};
    for (const char* name : names) {
        auto value = tree.value().property(BOARD_GMAC_DT_NODE, name);
        if (value.ok() && value.value().size == dwc_eqos::MAC_BYTES)
            return value.value().data;
    }
    klog::warn("net: device tree has no address for %s\n", BOARD_GMAC_DT_NODE);
    return nullptr;
}

}  // namespace

namespace jh7110_gmac {

int init() {
    if constexpr (BOARD_JH7110_GMAC) {
        enable_bus();
        Error error = dwc_eqos::attach(BOARD_GMAC_PHYS + KERNEL_BASE, board_address());
        if (error != Error::None)
            klog::warn("net: GMAC1 not attached (%s)\n", error_str(error));
        return static_cast<int>(error);
    } else {
        return 0;
    }
}

}  // namespace jh7110_gmac
