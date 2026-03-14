/**
 * @file trapentry.S
 * @brief AArch64 exception vector table and trap entry/exit.
 *
 * AArch64 requires a 2KB-aligned exception vector table installed
 * via VBAR_EL1.  Each vector entry is exactly 128 bytes (32 insns).
 *
 * The table has 4 groups × 4 vectors:
 *   Group 0: Current EL with SP_EL0  (not used in our kernel)
 *   Group 1: Current EL with SP_ELx  (kernel exceptions)
 *   Group 2: Lower EL, AArch64       (user-mode traps)
 *   Group 3: Lower EL, AArch32       (not used)
 *
 * Each group has: Synchronous, IRQ, FIQ, SError
 *
 * TrapFrame layout (must match struct TrapFrame in trapframe.h):
 *   0x000..0x0F0:  x0-x30     (31 regs × 8 bytes = 248)
 *   0x0F8:         sp         (SP_EL0)
 *   0x100:         pc         (ELR_EL1)
 *   0x108:         pstate     (SPSR_EL1)
 *   0x110:         esr        (ESR_EL1)
 *   0x118:         far        (FAR_EL1)
 *   Total: 0x120 = 288 bytes
 */

/* ================================================================
 * Macro: save all registers into TrapFrame on the kernel stack
 * ================================================================ */
.macro SAVE_ALL
    /* Allocate TrapFrame on the stack */
    sub  sp, sp, #0x120

    /* Save x0-x30 */
    stp  x0,  x1,  [sp, #0x000]
    stp  x2,  x3,  [sp, #0x010]
    stp  x4,  x5,  [sp, #0x020]
    stp  x6,  x7,  [sp, #0x030]
    stp  x8,  x9,  [sp, #0x040]
    stp  x10, x11, [sp, #0x050]
    stp  x12, x13, [sp, #0x060]
    stp  x14, x15, [sp, #0x070]
    stp  x16, x17, [sp, #0x080]
    stp  x18, x19, [sp, #0x090]
    stp  x20, x21, [sp, #0x0A0]
    stp  x22, x23, [sp, #0x0B0]
    stp  x24, x25, [sp, #0x0C0]
    stp  x26, x27, [sp, #0x0D0]
    stp  x28, x29, [sp, #0x0E0]
    str  x30,      [sp, #0x0F0]

    /* Save SP_EL0 (user stack pointer) */
    mrs  x10, sp_el0
    str  x10, [sp, #0x0F8]

    /* Save ELR_EL1 (return address) */
    mrs  x10, elr_el1
    str  x10, [sp, #0x100]

    /* Save SPSR_EL1 (saved processor state) */
    mrs  x10, spsr_el1
    str  x10, [sp, #0x108]

    /* Save ESR_EL1 (exception syndrome) */
    mrs  x10, esr_el1
    str  x10, [sp, #0x110]

    /* Save FAR_EL1 (fault address) */
    mrs  x10, far_el1
    str  x10, [sp, #0x118]
.endm

/* ================================================================
 * Exception vector table — must be 2KB aligned
 * Each entry is exactly 128 bytes (0x80).
 * ================================================================ */
.section .text
.balign 2048
.globl _vectors
_vectors:

/* ---- Group 0: Current EL with SP_EL0 (unused) ---- */
/* 0x000: Synchronous */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x080: IRQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x100: FIQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x180: SError */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* ---- Group 1: Current EL with SP_ELx (kernel traps) ---- */
/* 0x200: Synchronous */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x280: IRQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x300: FIQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x380: SError */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* ---- Group 2: Lower EL, AArch64 (user-mode traps) ---- */
/* 0x400: Synchronous */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x480: IRQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x500: FIQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x580: SError */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* ---- Group 3: Lower EL, AArch32 (unused) ---- */
/* 0x600: Synchronous */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x680: IRQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x700: FIQ */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

/* 0x780: SError */
    SAVE_ALL
    mov  x0, sp
    bl   trap
    b    trapret
.balign 0x80

.section .note.GNU-stack,"",@progbits
