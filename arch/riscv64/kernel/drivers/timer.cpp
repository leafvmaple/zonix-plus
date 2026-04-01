/**
 * SBI timer for RISC-V supervisor mode.
 *
 * The SBI TIME extension (EID 0x54494D45 "TIME"):
 *   FID 0 = SET_TIMER: set the next timer interrupt.
 *   a7 = EID, a6 = FID, a0 = stime_value
 *
 * OpenSBI implements both the v0.2 TIME extension and the legacy
 * interface (a7=0).  We use the TIME extension for correctness.
 *
 * After calling sbi_set_timer, the SBI clears the STIP bit in sip
 * and will raise it again when mtime >= stime_value.
 * The kernel must have SIE.STIE set to receive the interrupt.
 */

#include "timer.h"
#include <asm/cpu.h>
#include <base/types.h>

namespace {

/* SBI standard extension ID for TIME */
constexpr uint64_t SBI_EID_TIME = 0x54494D45ULL;
constexpr uint64_t SBI_FID_SET_TIMER = 0;

static inline uint64_t sbi_set_timer(uint64_t stime) {
    uint64_t error;
    __asm__ volatile("li a7, %2\n"
                     "li a6, %3\n"
                     "mv a0, %1\n"
                     "ecall\n"
                     "mv %0, a0\n"
                     : "=r"(error)
                     : "r"(stime), "i"(SBI_EID_TIME), "i"(SBI_FID_SET_TIMER)
                     : "a0", "a6", "a7", "memory");
    return error;
}

}  // namespace

namespace timer {

volatile int64_t ticks = 0;

uint64_t now() {
    uint64_t t;
    __asm__ volatile("csrr %0, time" : "=r"(t));
    return t;
}

void set_next() {
    sbi_set_timer(now() + TIMER_TICK_TICKS);
}

int init() {
    /* Enable supervisor timer interrupts */
    __asm__ volatile("csrs sie, %0" : : "r"(SIE_STIE) : "memory");

    /* Arm the first tick */
    set_next();
    return 0;
}

}  // namespace timer
