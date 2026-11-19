// The Goldfish real-time clock of the QEMU virt machine. Boards without one
// set BOARD_RTC_PHYS to zero.

#include "drivers/mmio.h"

#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>

namespace {

// Nanoseconds since 1970-01-01 UTC. Reading the low half latches the high half.
constexpr uint32_t TIME_LOW = 0x00;
constexpr uint32_t TIME_HIGH = 0x04;
constexpr uint64_t NANOSECONDS = 1000000000ULL;

}  // namespace

bool arch_read_wall_clock(uint64_t* seconds) {
    constexpr uintptr_t rtc_phys = BOARD_RTC_PHYS;
    if (rtc_phys == 0)
        return false;
    const uintptr_t base = rtc_phys + KERNEL_BASE;
    const uint64_t low = mmio::read32(base, TIME_LOW);
    const uint64_t high = mmio::read32(base, TIME_HIGH);
    *seconds = ((high << 32) | low) / NANOSECONDS;
    return true;
}
