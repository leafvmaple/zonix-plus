#include <asm/power.h>
#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>

#include "drivers/intr.h"
#include "drivers/mmio.h"

namespace {

constexpr uint64_t SBI_EID_SYSTEM_RESET = 0x53525354;
constexpr uint64_t SBI_FID_SYSTEM_RESET = 0;
constexpr uint64_t SBI_RESET_SHUTDOWN = 0;
constexpr uint64_t SBI_RESET_COLD_REBOOT = 1;
constexpr uint64_t SBI_RESET_REASON_NONE = 0;
constexpr int64_t SBI_NOT_SUPPORTED = -2;
constexpr int64_t SBI_INVALID_PARAM = -3;

// JH7110 system clock and reset generator layout: one register per clock
// with its gate in the top bit, then the reset assert bitmaps.
constexpr uint32_t CLOCK_GATE_ENABLE = 1U << 31;
constexpr uintptr_t RESET_ASSERT_OFFSET = 0x2F8;

// DesignWare I2C registers. OpenSBI's driver programs only the target address
// and the enable bit, so the controller must already be in 7-bit
// standard-speed master mode, as firmware left it before the OS booted.
constexpr uintptr_t I2C_CON = 0x00;
constexpr uintptr_t I2C_ENABLE = 0x6C;
constexpr uintptr_t I2C_ENABLE_STATUS = 0x9C;
constexpr uint32_t I2C_CON_MASTER = 1U << 0;
constexpr uint32_t I2C_CON_SPEED_STANDARD = 1U << 1;
constexpr uint32_t I2C_CON_RESTART_ENABLE = 1U << 5;
constexpr uint32_t I2C_CON_SLAVE_DISABLE = 1U << 6;
constexpr uint32_t I2C_ENABLED = 1U << 0;
constexpr int I2C_DISABLE_POLLS = 100000;

// Firmware that resets the board through an I2C power controller needs that
// controller running. Starting the OS gated its clock and returned the
// controller to its reset defaults (high speed, 10-bit addressing).
void prepare_board_reset() {
    constexpr uintptr_t syscrg_phys = BOARD_SYSCRG_PHYS;
    if constexpr (syscrg_phys != 0) {
        const uintptr_t crg = syscrg_phys + KERNEL_BASE;
        const uintptr_t clock = static_cast<uintptr_t>(BOARD_PMIC_I2C_CLOCK) * sizeof(uint32_t);
        const uintptr_t reset = RESET_ASSERT_OFFSET + BOARD_PMIC_I2C_RESET / 32 * sizeof(uint32_t);
        const uint32_t reset_bit = 1U << (BOARD_PMIC_I2C_RESET % 32);
        mmio::write32(crg, clock, mmio::read32(crg, clock) | CLOCK_GATE_ENABLE);
        mmio::write32(crg, reset, mmio::read32(crg, reset) & ~reset_bit);
        arch_mb();

        // The configuration registers accept writes only while disabled.
        const uintptr_t i2c = BOARD_PMIC_I2C_PHYS + KERNEL_BASE;
        mmio::write32(i2c, I2C_ENABLE, 0);
        for (int i = 0; i < I2C_DISABLE_POLLS && (mmio::read32(i2c, I2C_ENABLE_STATUS) & I2C_ENABLED) != 0; ++i)
            arch_spin_hint();
        mmio::write32(i2c, I2C_CON,
                      I2C_CON_MASTER | I2C_CON_SPEED_STANDARD | I2C_CON_RESTART_ENABLE | I2C_CON_SLAVE_DISABLE);
        arch_mb();
    }
}

Error system_reset(uint64_t type) {
    intr::Guard guard;
    prepare_board_reset();
    register uint64_t error __asm__("a0") = type;
    register uint64_t reason __asm__("a1") = SBI_RESET_REASON_NONE;
    register uint64_t function_id __asm__("a6") = SBI_FID_SYSTEM_RESET;
    register uint64_t extension_id __asm__("a7") = SBI_EID_SYSTEM_RESET;
    __asm__ volatile("ecall" : "+r"(error), "+r"(reason) : "r"(function_id), "r"(extension_id) : "memory");
    if (error == 0)
        arch_halt_forever();
    const int64_t status = static_cast<int64_t>(error);
    if (status == SBI_NOT_SUPPORTED)
        return Error::NotSupported;
    return status == SBI_INVALID_PARAM ? Error::Invalid : Error::Fail;
}

}  // namespace

Error arch_power_off() {
    return system_reset(SBI_RESET_SHUTDOWN);
}

Error arch_reboot() {
    return system_reset(SBI_RESET_COLD_REBOOT);
}
