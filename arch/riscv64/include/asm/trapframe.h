#pragma once

/*
 * RISC-V supervisor trap frame.
 *
 * Saved on the kernel stack by trapentry.S whenever an exception or
 * interrupt fires in S-mode (or comes from U-mode via ecall / page-fault).
 *
 * Layout must exactly match the SAVE_ALL / RESTORE_ALL macros in
 * arch/riscv64/kernel/trapentry.S.
 *
 * Register ABI mapping:
 *   x0  — zero (always 0, saved for uniform indexing)
 *   x1  — ra   (return address)
 *   x2  — sp   (stack pointer, saved separately)
 *   x3  — gp   (global pointer)
 *   x4  — tp   (thread pointer)
 *   x5  — t0
 *   x6  — t1
 *   x7  — t2
 *   x8  — s0 / fp
 *   x9  — s1
 *   x10 — a0   (syscall return / arg 0)
 *   x11 — a1   (arg 1)
 *   x12 — a2
 *   x13 — a3
 *   x14 — a4
 *   x15 — a5
 *   x16 — a6
 *   x17 — a7   (syscall number)
 *   x18 — s2
 *   ...
 *   x27 — s11
 *   x28 — t3
 *   x29 — t4
 *   x30 — t5
 *   x31 — t6
 */

#ifndef __ASSEMBLY__

#include <base/types.h>

struct TrapFrame {
    uint64_t regs[32]{}; /* x0–x31 (x0 is always 0) */

    /*
     * Supervisor CSRs captured at trap entry.
     * sepc    — address of faulting / next instruction
     * sstatus — supervisor status register (SPP, SPIE, SIE …)
     * scause  — exception cause code (bit 63 = interrupt)
     * stval   — fault address (page fault) or offending insn (illegal insn)
     */
    uint64_t sepc{};
    uint64_t sstatus{};
    uint64_t scause{};
    uint64_t stval{};

    void print() const;
    void print_pgfault() const;

    /* Syscall interface */
    [[nodiscard]] uint64_t syscall_nr() const { return regs[17]; } /* a7 */
    [[nodiscard]] uint64_t syscall_arg(int n) const {
        if (n >= 0 && n <= 5) {
            return regs[10 + n]; /* a0–a5 */
        }
        return 0;
    }
    void set_return(uint64_t val) { regs[10] = val; } /* a0 */
};

/* Byte offsets used by trapentry.S — must stay in sync */
#define TF_X0      0
#define TF_RA      8   /* x1  */
#define TF_SP      16  /* x2  */
#define TF_GP      24  /* x3  */
#define TF_TP      32  /* x4  */
#define TF_T0      40  /* x5  */
#define TF_T1      48  /* x6  */
#define TF_T2      56  /* x7  */
#define TF_S0      64  /* x8  */
#define TF_S1      72  /* x9  */
#define TF_A0      80  /* x10 */
#define TF_A1      88  /* x11 */
#define TF_A2      96  /* x12 */
#define TF_A3      104 /* x13 */
#define TF_A4      112 /* x14 */
#define TF_A5      120 /* x15 */
#define TF_A6      128 /* x16 */
#define TF_A7      136 /* x17 */
#define TF_S2      144 /* x18 */
#define TF_S3      152 /* x19 */
#define TF_S4      160 /* x20 */
#define TF_S5      168 /* x21 */
#define TF_S6      176 /* x22 */
#define TF_S7      184 /* x23 */
#define TF_S8      192 /* x24 */
#define TF_S9      200 /* x25 */
#define TF_S10     208 /* x26 */
#define TF_S11     216 /* x27 */
#define TF_T3      224 /* x28 */
#define TF_T4      232 /* x29 */
#define TF_T5      240 /* x30 */
#define TF_T6      248 /* x31 */
#define TF_SEPC    256
#define TF_SSTATUS 264
#define TF_SCAUSE  272
#define TF_STVAL   280
#define TF_SIZE    288 /* total frame size (must be 16-byte aligned) */

extern "C" void trap_dispatch(TrapFrame* tf);
extern "C" void trapret(void);

#else /* __ASSEMBLY__ */

/* Same offsets for use in .S files */
#define TF_X0      0
#define TF_RA      8
#define TF_SP      16
#define TF_GP      24
#define TF_TP      32
#define TF_T0      40
#define TF_T1      48
#define TF_T2      56
#define TF_S0      64
#define TF_S1      72
#define TF_A0      80
#define TF_A1      88
#define TF_A2      96
#define TF_A3      104
#define TF_A4      112
#define TF_A5      120
#define TF_A6      128
#define TF_A7      136
#define TF_S2      144
#define TF_S3      152
#define TF_S4      160
#define TF_S5      168
#define TF_S6      176
#define TF_S7      184
#define TF_S8      192
#define TF_S9      200
#define TF_S10     208
#define TF_S11     216
#define TF_T3      224
#define TF_T4      232
#define TF_T5      240
#define TF_T6      248
#define TF_SEPC    256
#define TF_SSTATUS 264
#define TF_SCAUSE  272
#define TF_STVAL   280
#define TF_SIZE    288

#endif /* !__ASSEMBLY__ */
