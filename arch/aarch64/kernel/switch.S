#include <asm/context.h>
#include <asm/trapframe.h>

/**
 * @file switch.S
 * @brief AArch64 context switch and trap return routines.
 *
 * switch_to(Context* from, Context* to):
 *   Save callee-saved registers into 'from', restore from 'to'.
 *   Layout must match struct Context in asm/context.h.
 *
 * forkret:
 *   Entry point for newly forked processes. SP points to the
 *   TrapFrame set up by copy_thread(). We jump to trapret to
 *   restore the full register state and return to user/kernel.
 */

.section .text

/* The kernel is built with -mgeneral-regs-only, which also hides the FP/SIMD
 * instructions from the assembler. switch_to alone moves that state. */
.arch_extension fp
.arch_extension simd

/* -------------------------------------------------------------------
 * void switch_to(Context* from, Context* to)
 *   x0 = pointer to 'from' Context
 *   x1 = pointer to 'to'   Context
 *
 * Context layout (offsets, each 8 bytes):
 *   0x00 x19,  0x08 x20,  0x10 x21,  0x18 x22,
 *   0x20 x23,  0x28 x24,  0x30 x25,  0x38 x26,
 *   0x40 x27,  0x48 x28,  0x50 x29(FP), 0x58 x30(LR),
 *   0x60 SP,
 *   CONTEXT_FP_STATE_OFFSET: q0-q31, then FPCR and FPSR
 *
 * FP/SIMD state is switched eagerly, including on switches from an IRQ or
 * a syscall. The kernel is built with -mgeneral-regs-only, so the vector
 * registers still hold the outgoing task's values here.
 * ------------------------------------------------------------------- */
.globl switch_to
switch_to:
    add  x9, x0, #CONTEXT_FP_STATE_OFFSET
    stp  q0,  q1,  [x9, #0x000]
    stp  q2,  q3,  [x9, #0x020]
    stp  q4,  q5,  [x9, #0x040]
    stp  q6,  q7,  [x9, #0x060]
    stp  q8,  q9,  [x9, #0x080]
    stp  q10, q11, [x9, #0x0A0]
    stp  q12, q13, [x9, #0x0C0]
    stp  q14, q15, [x9, #0x0E0]
    stp  q16, q17, [x9, #0x100]
    stp  q18, q19, [x9, #0x120]
    stp  q20, q21, [x9, #0x140]
    stp  q22, q23, [x9, #0x160]
    stp  q24, q25, [x9, #0x180]
    stp  q26, q27, [x9, #0x1A0]
    stp  q28, q29, [x9, #0x1C0]
    stp  q30, q31, [x9, #0x1E0]
    mrs  x10, fpcr
    mrs  x11, fpsr
    str  x10, [x9, #FP_STATE_CONTROL_OFFSET]
    str  x11, [x9, #(FP_STATE_CONTROL_OFFSET + 8)]

    add  x9, x1, #CONTEXT_FP_STATE_OFFSET
    ldp  q0,  q1,  [x9, #0x000]
    ldp  q2,  q3,  [x9, #0x020]
    ldp  q4,  q5,  [x9, #0x040]
    ldp  q6,  q7,  [x9, #0x060]
    ldp  q8,  q9,  [x9, #0x080]
    ldp  q10, q11, [x9, #0x0A0]
    ldp  q12, q13, [x9, #0x0C0]
    ldp  q14, q15, [x9, #0x0E0]
    ldp  q16, q17, [x9, #0x100]
    ldp  q18, q19, [x9, #0x120]
    ldp  q20, q21, [x9, #0x140]
    ldp  q22, q23, [x9, #0x160]
    ldp  q24, q25, [x9, #0x180]
    ldp  q26, q27, [x9, #0x1A0]
    ldp  q28, q29, [x9, #0x1C0]
    ldp  q30, q31, [x9, #0x1E0]
    ldr  x10, [x9, #FP_STATE_CONTROL_OFFSET]
    ldr  x11, [x9, #(FP_STATE_CONTROL_OFFSET + 8)]
    msr  fpcr, x10
    msr  fpsr, x11

    /* Save callee-saved registers into *from (x0) */
    stp  x19, x20, [x0, #0x00]
    stp  x21, x22, [x0, #0x10]
    stp  x23, x24, [x0, #0x20]
    stp  x25, x26, [x0, #0x30]
    stp  x27, x28, [x0, #0x40]
    stp  x29, x30, [x0, #0x50]
    mov  x9, sp
    str  x9,       [x0, #0x60]

    /* Restore callee-saved registers from *to (x1) */
    ldp  x19, x20, [x1, #0x00]
    ldp  x21, x22, [x1, #0x10]
    ldp  x23, x24, [x1, #0x20]
    ldp  x25, x26, [x1, #0x30]
    ldp  x27, x28, [x1, #0x40]
    ldp  x29, x30, [x1, #0x50]
    ldr  x9,       [x1, #0x60]
    mov  sp, x9

    ret  /* returns to the address in x30 (LR) */

/* -------------------------------------------------------------------
 * forkret — entry point for new processes after fork/kernel_thread.
 *
 * The scheduler calls switch_to() which restores LR = forkret.
 * SP was set to point at the TrapFrame.  Fall through to trapret.
 * ------------------------------------------------------------------- */
.globl forkret
forkret:
    /* SP already points to the TrapFrame.  Fall through to trapret. */
    b trapret

/* -------------------------------------------------------------------
 * trapret — restore full register state from TrapFrame and eret.
 *
 * TrapFrame layout (must match struct TrapFrame in trapframe.h):
 *   offsets 0x000..0x0F0:  x0-x30  (31 * 8 = 248 bytes)
 *   offset  0x0F8:         sp      (SP_EL0)
 *   offset  0x100:         pc      (ELR_EL1)
 *   offset  0x108:         pstate  (SPSR_EL1)
 *   offset  0x110:         esr     (ESR_EL1, not restored)
 *   offset  0x118:         far     (FAR_EL1, not restored)
 * ------------------------------------------------------------------- */
.globl trapret
trapret:
    /* Restore ELR_EL1 and SPSR_EL1 */
    ldr  x10, [sp, #0x100]       /* pc -> ELR_EL1 */
    ldr  x11, [sp, #0x108]       /* pstate -> SPSR_EL1 */
    msr  elr_el1, x10
    msr  spsr_el1, x11

    /* Restore SP_EL0 (user stack pointer) */
    ldr  x10, [sp, #0x0F8]
    msr  sp_el0, x10

    /* Restore x0-x30 */
    ldp  x0,  x1,  [sp, #0x000]
    ldp  x2,  x3,  [sp, #0x010]
    ldp  x4,  x5,  [sp, #0x020]
    ldp  x6,  x7,  [sp, #0x030]
    ldp  x8,  x9,  [sp, #0x040]
    ldp  x10, x11, [sp, #0x050]
    ldp  x12, x13, [sp, #0x060]
    ldp  x14, x15, [sp, #0x070]
    ldp  x16, x17, [sp, #0x080]
    ldp  x18, x19, [sp, #0x090]
    ldp  x20, x21, [sp, #0x0A0]
    ldp  x22, x23, [sp, #0x0B0]
    ldp  x24, x25, [sp, #0x0C0]
    ldp  x26, x27, [sp, #0x0D0]
    ldp  x28, x29, [sp, #0x0E0]
    ldr  x30,      [sp, #0x0F0]

    /* Advance SP past the 16-byte-aligned TrapFrame */
    add  sp, sp, #ARM64_TRAP_FRAME_SIZE

    eret

.section .note.GNU-stack,"",@progbits
