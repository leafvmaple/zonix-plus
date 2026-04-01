/**
 * @file trapentry.S
 * @brief RISC-V supervisor trap entry points.
 *
 * Two separate trap handlers are installed depending on whether the CPU
 * is in S-mode (kernel) or U-mode (user):
 *
 *   kernel_trap_vec  — installed in stvec while the kernel runs (SPP=1).
 *                      sp is already a valid kernel stack pointer.
 *
 *   user_trap_vec    — installed in stvec before sret to user mode.
 *                      sscratch = kernel stack top; sp = user stack.
 *
 * Both handlers build a complete struct TrapFrame on the kernel stack,
 * call trap_dispatch(TrapFrame*), then jump to trapret (in switch.S).
 *
 * TrapFrame layout (TF_* offsets from trapframe.h, TF_SIZE = 288):
 *   TF_X0  (  0): x0  = 0 (always)
 *   TF_RA  (  8): x1  ra
 *   TF_SP  ( 16): x2  sp  (kernel or user sp — see below)
 *   TF_GP  ( 24): x3  gp
 *   TF_TP  ( 32): x4  tp
 *   TF_T0  ( 40): x5  t0
 *   TF_T1  ( 48): x6  t1
 *   TF_T2  ( 56): x7  t2
 *   TF_S0  ( 64): x8  s0/fp
 *   TF_S1  ( 72): x9  s1
 *   TF_A0  ( 80): x10 a0
 *   ...
 *   TF_T6  (248): x31 t6
 *   TF_SEPC    (256)
 *   TF_SSTATUS (264)
 *   TF_SCAUSE  (272)
 *   TF_STVAL   (280)
 *
 * sscratch convention:
 *   In S-mode:  sscratch = 0  (sentinel "we are in kernel")
 *   In U-mode:  sscratch = top of kernel stack for this task
 *               (set by arch_switch_rsp0 / trapret before sret to user)
 */

#include <asm/trapframe.h>  /* TF_* offsets, TF_SIZE */

/* ================================================================
 * kernel_trap_vec — trap handler while executing in S-mode.
 *
 * On entry:
 *   sp    = valid kernel stack pointer (unchanged)
 *   sscratch = 0 (sentinel)
 *   All x1-x31 have their original kernel values.
 *
 * We temporarily park the original sp in sscratch so that the
 * frame allocation does not destroy it, then save it into TF_SP.
 *
 * NOTE: We save x5 (t0) BEFORE using t0 for any CSR operations,
 *       because after `sd x5, TF_T0(sp)` the slot is occupied and
 *       we can freely reuse the t0 register.
 * ================================================================ */
.section .text
.align 2
.globl kernel_trap_vec
.type  kernel_trap_vec, @function
kernel_trap_vec:
    /* Park original sp in sscratch (interrupts are off on trap entry) */
    csrw  sscratch, sp
    addi  sp, sp, -TF_SIZE

    /* Save x5 (t0) FIRST so we can freely use t0 as a scratch register */
    sd    x5,  TF_T0(sp)

    /* Retrieve and save the original kernel sp from sscratch */
    csrr  t0, sscratch
    sd    t0,  TF_SP(sp)
    csrw  sscratch, zero     /* restore sentinel (in kernel) */

    /* Save x0 = 0 */
    sd    zero, TF_X0(sp)

    /* Save x1-x4 */
    sd    x1,  TF_RA(sp)
    /* x2/sp already saved above */
    sd    x3,  TF_GP(sp)
    sd    x4,  TF_TP(sp)
    /* x5/t0 already saved above */

    /* Save x6-x31 */
    sd    x6,  TF_T1(sp)
    sd    x7,  TF_T2(sp)
    sd    x8,  TF_S0(sp)
    sd    x9,  TF_S1(sp)
    sd    x10, TF_A0(sp)
    sd    x11, TF_A1(sp)
    sd    x12, TF_A2(sp)
    sd    x13, TF_A3(sp)
    sd    x14, TF_A4(sp)
    sd    x15, TF_A5(sp)
    sd    x16, TF_A6(sp)
    sd    x17, TF_A7(sp)
    sd    x18, TF_S2(sp)
    sd    x19, TF_S3(sp)
    sd    x20, TF_S4(sp)
    sd    x21, TF_S5(sp)
    sd    x22, TF_S6(sp)
    sd    x23, TF_S7(sp)
    sd    x24, TF_S8(sp)
    sd    x25, TF_S9(sp)
    sd    x26, TF_S10(sp)
    sd    x27, TF_S11(sp)
    sd    x28, TF_T3(sp)
    sd    x29, TF_T4(sp)
    sd    x30, TF_T5(sp)
    sd    x31, TF_T6(sp)

    /* Save supervisor CSRs (t0 is free to use as scratch) */
    csrr  t0, sepc;    sd t0, TF_SEPC(sp)
    csrr  t0, sstatus; sd t0, TF_SSTATUS(sp)
    csrr  t0, scause;  sd t0, TF_SCAUSE(sp)
    csrr  t0, stval;   sd t0, TF_STVAL(sp)

    /* call trap_dispatch(TrapFrame* tf) */
    mv    a0, sp
    call  trap_dispatch

    j     trapret

/* ================================================================
 * user_trap_vec — trap handler while executing in U-mode.
 *
 * On entry:
 *   sp       = user stack pointer
 *   sscratch = top of kernel stack for this task
 *              (set by arch_switch_rsp0 / trapret)
 *   All x1-x31 have their original user values.
 *
 * We swap sp and sscratch to get the kernel stack pointer into sp,
 * then save x5 (t0) FIRST, retrieve user_sp from sscratch via t0,
 * and save it into TF_SP.
 * ================================================================ */
.align 2
.globl user_trap_vec
.type  user_trap_vec, @function
user_trap_vec:
    /* Swap sp ↔ sscratch  →  sp = kernel_sp,  sscratch = user_sp */
    csrrw sp, sscratch, sp
    addi  sp, sp, -TF_SIZE

    /* Save x5 (t0) FIRST before we use t0 as a scratch register */
    sd    x5,  TF_T0(sp)

    /* Retrieve user_sp from sscratch and save it into TF_SP */
    csrr  t0, sscratch
    sd    t0,  TF_SP(sp)
    csrw  sscratch, zero     /* sentinel: now in kernel */

    /* Save x0 = 0 */
    sd    zero, TF_X0(sp)

    /* Save x1-x4 */
    sd    x1,  TF_RA(sp)
    /* x2/sp already saved above */
    sd    x3,  TF_GP(sp)
    sd    x4,  TF_TP(sp)
    /* x5/t0 already saved above */

    /* Save x6-x31 */
    sd    x6,  TF_T1(sp)
    sd    x7,  TF_T2(sp)
    sd    x8,  TF_S0(sp)
    sd    x9,  TF_S1(sp)
    sd    x10, TF_A0(sp)
    sd    x11, TF_A1(sp)
    sd    x12, TF_A2(sp)
    sd    x13, TF_A3(sp)
    sd    x14, TF_A4(sp)
    sd    x15, TF_A5(sp)
    sd    x16, TF_A6(sp)
    sd    x17, TF_A7(sp)
    sd    x18, TF_S2(sp)
    sd    x19, TF_S3(sp)
    sd    x20, TF_S4(sp)
    sd    x21, TF_S5(sp)
    sd    x22, TF_S6(sp)
    sd    x23, TF_S7(sp)
    sd    x24, TF_S8(sp)
    sd    x25, TF_S9(sp)
    sd    x26, TF_S10(sp)
    sd    x27, TF_S11(sp)
    sd    x28, TF_T3(sp)
    sd    x29, TF_T4(sp)
    sd    x30, TF_T5(sp)
    sd    x31, TF_T6(sp)

    /* Restore stvec to kernel trap vector (we are now in S-mode) */
    la    t0, kernel_trap_vec
    csrw  stvec, t0

    /* Save supervisor CSRs */
    csrr  t0, sepc;    sd t0, TF_SEPC(sp)
    csrr  t0, sstatus; sd t0, TF_SSTATUS(sp)
    csrr  t0, scause;  sd t0, TF_SCAUSE(sp)
    csrr  t0, stval;   sd t0, TF_STVAL(sp)

    /* call trap_dispatch(TrapFrame* tf) */
    mv    a0, sp
    call  trap_dispatch

    j     trapret

.section .note.GNU-stack,"",@progbits
