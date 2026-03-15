#pragma once

/**
 * x86_64 Task State Segment (TSS) management.
 *
 * In long mode the TSS is *not* used for hardware task-switching;
 * its only mandatory job is to supply the RSP0 that the CPU loads
 * when transitioning from ring 3 → ring 0 on an interrupt or
 * exception (and optionally IST stacks for NMI / double-fault).
 *
 * Call tss::init() once during boot (after GDT is live) and
 * tss::set_rsp0() on every context switch to a user-mode process.
 */

#include <base/types.h>

namespace tss {

/**
 * Initialise the TSS, write its descriptor into GDT slots 5-6,
 * and execute LTR to activate it.
 */
int init();

/**
 * Update TSS.RSP0 — the stack the CPU will switch to when entering
 * ring 0 from ring 3.  Call this before resuming a user-mode process.
 *
 * @param rsp0  Kernel-mode stack pointer (top of the process's kstack)
 */
void set_rsp0(uintptr_t rsp0);

}  // namespace tss
