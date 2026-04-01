#pragma once

/**
 * SBI timer driver for RISC-V.
 *
 * Uses the SBI TIME extension (a7=0x54494D45, a6=0 SET_TIMER) to
 * arm the next supervisor timer interrupt, or the legacy SET_TIMER
 * ecall (a7=0) when the TIME extension is unavailable.
 */

#include <base/types.h>

namespace timer {

/* Timer clock frequency on QEMU virt RISC-V (10 MHz) */
static constexpr uint64_t TIMER_FREQ_HZ = 10000000ULL;

/* Ticks between scheduler interrupts (~10 ms) */
static constexpr uint64_t TIMER_TICK_TICKS = TIMER_FREQ_HZ / 100;

/* Initialise the timer and arm the first interrupt */
int init();

/* Arm the next timer interrupt */
void set_next();

/* Current value of the hardware time counter */
uint64_t now();

}  // namespace timer
