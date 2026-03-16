/**
 * @file timer.cpp
 * @brief ARM Generic Timer driver (virtual timer, CNTV).
 *
 * Configures the virtual timer at ~100 Hz and provides timer_set_next()
 * for rearming after each interrupt.
 */

#include "drivers/timer.h"
#include "drivers/gic.h"

namespace {

constexpr uint32_t VTIMER_INTID = 27;  // Virtual timer PPI

uint64_t cached_interval = 0;

}  // namespace

namespace timer {

volatile int64_t ticks = 0;

int init() {
    uint64_t freq{};
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0)
        return -1;

    cached_interval = freq / 100;  // 100 Hz = 10ms per tick
    if (cached_interval == 0)
        return -1;

    // Enable virtual timer PPI in GIC
    gic::enable(VTIMER_INTID);

    // Arm the timer
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(cached_interval));
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1UL));

    return 0;
}

void set_next() {
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(cached_interval));
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1UL));
}

}  // namespace timer
