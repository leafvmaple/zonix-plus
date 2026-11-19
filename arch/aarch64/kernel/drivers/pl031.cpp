// The PL031 real-time clock of the QEMU virt machine.

#include "drivers/mmio.h"

#include <asm/arch.h>
#include <asm/mmu.h>
#include <asm/page.h>

namespace {

constexpr uintptr_t RTC_PHYS = 0x09010000;
// The data register counts seconds since 1970-01-01 UTC.
constexpr uint32_t RTCDR = 0x000;

}  // namespace

bool arch_read_wall_clock(uint64_t* seconds) {
    *seconds = mmio::read32(reinterpret_cast<uintptr_t>(phys_to_virt<void>(RTC_PHYS)), RTCDR);
    return true;
}
