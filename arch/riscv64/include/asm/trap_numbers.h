#pragma once

/*
 * RISC-V trap/interrupt cause codes (scause register).
 *
 * Bit 63 of scause = 1  → interrupt
 * Bit 63 of scause = 0  → exception (synchronous)
 */

#ifndef __ASSEMBLY__

#include <base/types.h>

/* scause interrupt bit */
inline constexpr uint64_t SCAUSE_INTR_BIT = 1ULL << 63;

/* ---------- Synchronous exception codes (bit 63 = 0) ---------- */
inline constexpr int CAUSE_MISALIGNED_FETCH = 0;
inline constexpr int CAUSE_FETCH_ACCESS = 1;
inline constexpr int CAUSE_ILLEGAL_INSN = 2;
inline constexpr int CAUSE_BREAKPOINT = 3;
inline constexpr int CAUSE_MISALIGNED_LOAD = 4;
inline constexpr int CAUSE_LOAD_ACCESS = 5;
inline constexpr int CAUSE_MISALIGNED_STORE = 6;
inline constexpr int CAUSE_STORE_ACCESS = 7;
inline constexpr int CAUSE_USER_ECALL = 8;       /* ecall from U-mode  */
inline constexpr int CAUSE_SUPERVISOR_ECALL = 9; /* ecall from S-mode  */
inline constexpr int CAUSE_FETCH_PAGE_FAULT = 12;
inline constexpr int CAUSE_LOAD_PAGE_FAULT = 13;
inline constexpr int CAUSE_STORE_PAGE_FAULT = 15;

/* ---------- Interrupt codes (bit 63 = 1) ---------- */
inline constexpr uint64_t IRQ_SUPERVISOR_SOFT = SCAUSE_INTR_BIT | 1;
inline constexpr uint64_t IRQ_SUPERVISOR_TIMER = SCAUSE_INTR_BIT | 5;
inline constexpr uint64_t IRQ_SUPERVISOR_EXT = SCAUSE_INTR_BIT | 9;

/* Generic aliases used by shared kernel code */
inline constexpr int T_SYSCALL = CAUSE_USER_ECALL;
inline constexpr int T_PGFLT = CAUSE_LOAD_PAGE_FAULT; /* representative; all three checked */

/* IRQ "line" numbers exposed to the generic layer.
 * For RISC-V we use the PLIC interrupt numbers directly. */
inline constexpr int IRQ_OFFSET = 0;
inline constexpr int IRQ_UART = 10; /* QEMU virt: UART0 → PLIC IRQ 10  */
inline constexpr int IRQ_COUNT = 64;

#endif /* !__ASSEMBLY__ */
