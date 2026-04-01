#pragma once

/**
 * PLIC (Platform-Level Interrupt Controller) for RISC-V QEMU virt.
 * Physical base: 0x0C000000.
 * Hart 0/S-mode context = 1.
 */

#include <base/types.h>

namespace plic {

/* Initialise PLIC for hart 0, S-mode */
int init();

/* Enable a specific interrupt source (1-based IRQ number) */
void enable(int irq);

/* Set the priority threshold for hart 0 S-mode context (0 = all pass) */
void set_threshold(uint32_t pri);

/*
 * Claim the highest-pending IRQ for hart 0 S-mode.
 * Returns the IRQ number (0 if no pending IRQ, i.e., spurious).
 */
uint32_t claim();

/* Signal completion of an IRQ to the PLIC */
void complete(uint32_t irq);

}  // namespace plic
