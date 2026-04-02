/**
 * @file head.S
 * @brief RISC-V (rv64gc) kernel entry point for UEFI boot.
 *
 * The UEFI bootloader (BOOTRISCV64.EFI) loads the kernel ELF at
 * BOARD_KERNEL_PHYS, fills a boot_info structure, calls
 * ExitBootServices, disables the MMU (satp = 0), and jumps here with:
 *
 *   a0 = hart ID
 *   a1 = physical address of boot_info
 *   CPU in S-mode, MMU OFF, interrupts disabled
 *
 * We then:
 *   1.  Park secondary harts (first hart to arrive continues).
 *   2.  Build a minimal Sv39 root page table (2 × 1GB gigapages).
 *   3.  Enable the MMU (write satp, sfence.vma).
 *   4.  Jump to the higher-half virtual address.
 *   5.  Remove the identity-map entry.
 *   6.  Zero BSS.
 *   7.  Copy boot_info to __kernel_boot_info (in .data).
 *   8.  Call kern_init(boot_info*).
 *
 * Page-table design (Sv39, 4KB granule, VA = PA + KERNEL_BASE):
 *
 *   __boot_pgd[BOARD_PGD_IDX_MMIO]  — 1GB gigapage, PA 0x0000_0000 → MMIO
 *   __boot_pgd[BOARD_PGD_IDX_DRAM]  — 1GB gigapage, PA BOARD_DRAM_BASE → DRAM
 *   __boot_pgd[BOARD_PGD_IDX_IDENT] — 1GB gigapage, PA BOARD_DRAM_BASE → identity
 *                                      (trampoline; removed after jump to virtual)
 */

#include <asm/page.h>       /* PTE_V/R/W/X/A/D, SATP_SV39 */
#include <asm/board.h>      /* BOARD_DRAM_BASE, BOARD_PGD_IDX_*, BOARD_KERNEL_PHYS */

#define BOOT_PERM  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D)  /* 0xCF */

/* ======================================================================
 * .text.boot — runs at PHYSICAL addresses before MMU is enabled.
 *
 * All symbol references use PC-relative addressing (la/call).  At this
 * stage PC = PA = VMA - KERNEL_BASE, so la resolves to PA, not VMA.
 * After enabling the MMU we manually add KERNEL_BASE to key registers.
 * ====================================================================== */
.section .text.boot, "ax"
.globl _start
_start:
    /* Disable all interrupts (clear SIE in sstatus) */
    csrci sstatus, 2

    /*
     * Park secondary harts — only the first hart to arrive runs boot.
     * Use an atomic flag so this works regardless of which hart ID
     * the firmware boots on (hart 0 on QEMU, hart 1 on VisionFive 2).
     */
    la    t0, __boot_hart_flag
    li    t1, 1
    amoswap.w.aq t1, t1, (t0)    /* t1 = old value; set flag to 1 */
    bnez  t1, .Lpark              /* if already claimed, park */

    /* Save boot_info phys pointer in s9 (callee-saved, survives calls) */
    mv    s9, a1

    /* ----------------------------------------------------------------
     * Set up a boot stack (physical address).
     * la resolves to PA while MMU is off.
     * ---------------------------------------------------------------- */
    la    sp, __boot_stack_top

    /* ----------------------------------------------------------------
     * Zero the boot page table (1 page = 512 × 8-byte slots).
     * ---------------------------------------------------------------- */
    la    t0, __boot_pgd          /* t0 = PA of root page table */
    li    t1, 4096
.Lzero_pgd:
    sd    zero, 0(t0)
    addi  t0, t0, 8
    addi  t1, t1, -8
    bnez  t1, .Lzero_pgd

    /* ----------------------------------------------------------------
     * Build PTE for 1GB gigapage: PA BOARD_DRAM_BASE (DRAM)
     *   pte = ((BOARD_DRAM_BASE >> 12) << 10) | BOOT_PERM
     * ---------------------------------------------------------------- */
    li    t2, ((BOARD_DRAM_BASE >> 12) << 10)
    li    t3, BOOT_PERM
    or    t2, t2, t3              /* t2 = 1GB gigapage PTE for PA BOARD_DRAM_BASE */

    la    t0, __boot_pgd

    /* PGD[BOARD_PGD_IDX_DRAM] = kernel DRAM */
    li    t3, BOARD_PGD_IDX_DRAM * 8
    add   t3, t0, t3
    sd    t2, 0(t3)

    /* PGD[BOARD_PGD_IDX_IDENT] = identity map trampoline (same PTE) */
    li    t3, BOARD_PGD_IDX_IDENT * 8
    add   t3, t0, t3
    sd    t2, 0(t3)

    /* ----------------------------------------------------------------
     * Build PTE for 1GB gigapage: PA 0x00000000 (low MMIO devices)
     * PGD[256] covers VA KERNEL_BASE .. KERNEL_BASE+1GB
     * That maps PLIC @0x0C000000, UART @0x10000000, etc.
     * ---------------------------------------------------------------- */
    li    t2, BOOT_PERM           /* PPN=0 (PA 0x00000000) */
    li    t3, BOARD_PGD_IDX_MMIO * 8
    add   t3, t0, t3
    sd    t2, 0(t3)

    /* ----------------------------------------------------------------
     * Write satp: mode=Sv39 (8 << 60), ASID=0, PPN=pgd_phys>>12
     * ---------------------------------------------------------------- */
    la    t0, __boot_pgd          /* PA of PGD */
    srli  t0, t0, 12             /* PPN */
    li    t1, SATP_SV39
    or    t0, t0, t1
    csrw  satp, t0
    sfence.vma zero, zero        /* flush all TLBs */

    /* ----------------------------------------------------------------
     * Jump to the higher-half virtual address.
     *
     * la t1, .Lvirt_start  → t1 = PHYSICAL addr of label (PC-relative
     *                         offsets are the same in PA and VA spaces)
     * add KERNEL_BASE       → t1 = VIRTUAL addr of label
     * jr t1                → execution continues in virtual space
     * ---------------------------------------------------------------- */
    li    t0, KERNEL_BASE
    la    t1, .Lvirt_start
    add   t1, t1, t0
    jr    t1

/* ======================================================================
 * From here we are executing in the higher-half virtual address space.
 * ====================================================================== */
.Lvirt_start:
    /* Fix stack pointer to virtual address */
    li    t0, KERNEL_BASE
    add   sp, sp, t0

    /* Remove identity map (PGD[BOARD_PGD_IDX_IDENT] = 0) */
    la    t0, __boot_pgd          /* virtual address (la → PA + KERNEL_BASE below) */
    /* Actually la still gives PA here because it's PC-relative and the
     * addend (symbol_VMA - insn_VMA) is fixed at link time.  We already
     * have the VMA in hand because PC is now VMA. Let's compute it:
     *   la gives: PC_actual + (sym_VMA - insn_VMA)
     *   PC_actual (virtual) = insn_VMA  → result = sym_VMA  ✓
     * So from here la directly gives the VIRTUAL address. */
    li    t1, BOARD_PGD_IDX_IDENT * 8
    add   t1, t0, t1
    sd    zero, 0(t1)
    sfence.vma zero, zero

    /* ----------------------------------------------------------------
     * Zero BSS
     * ---------------------------------------------------------------- */
    la    t0, __bss_start
    la    t1, __bss_end
    bgeu  t0, t1, .Lzero_bss_done
.Lzero_bss_loop:
    sd    zero, 0(t0)
    addi  t0, t0, 8
    bltu  t0, t1, .Lzero_bss_loop
.Lzero_bss_done:

    /* ----------------------------------------------------------------
     * Copy boot_info to __kernel_boot_info — only if a1 actually
     * points to a BootInfo (magic == 0x12345678).
     *
     * s9 = physical address of a1 (FDT or BootInfo).
     * VA = PA + KERNEL_BASE (our mapping: PA BOARD_DRAM_BASE → PGD[DRAM]).
     * BootInfo from UEFI bootloader is in DRAM range.
     * ---------------------------------------------------------------- */
    li    t0, KERNEL_BASE
    add   t0, s9, t0             /* t0 = PA + KERNEL_BASE = VA */
    lw    t1, 0(t0)              /* read first word */
    li    t2, 0x12345678         /* BOOT_INFO_MAGIC */
    bne   t1, t2, .Lskip_bi_copy

    la    a0, __kernel_boot_info /* dest */
    mv    a1, t0                 /* src (virtual) */
    li    a2, 256
.Lcopy_bi:
    ld    t0, 0(a1)
    sd    t0, 0(a0)
    addi  a0, a0, 8
    addi  a1, a1, 8
    addi  a2, a2, -8
    bnez  a2, .Lcopy_bi
.Lskip_bi_copy:

    /* ----------------------------------------------------------------
     * Run C++ global constructors.
     * Use callee-saved registers (s0/s1) so that jalr does not
     * clobber the loop variables (a0-a7 are caller-saved).
     * ---------------------------------------------------------------- */
    la    s0, __init_array_start
    la    s1, __init_array_end
    bgeu  s0, s1, .Lno_ctors
.Lctor_loop:
    ld    t0, 0(s0)
    jalr  t0
    addi  s0, s0, 8
    bltu  s0, s1, .Lctor_loop
.Lno_ctors:

    /* kern_init(boot_info*) */
    la    a0, __kernel_boot_info
    call  kern_init

    /* Should not return */
.Lpark:
    wfi
    j     .Lpark

/* ======================================================================
 * Root page table — 1 × 4KB page (512 × 8-byte entries), page-aligned.
 * Placed in .data.pgdir so the linker places it at a known PA.
 * ====================================================================== */
.section .data.pgdir, "aw", @progbits
.balign 4096
.globl __boot_pgd
.globl __boot_pml4   /* alias: vmm.cpp uses __boot_pml4 for non-aarch64 archs */
__boot_pgd:
__boot_pml4:
    .space 4096

/* ======================================================================
 * Boot stack — 16 KB, 16-byte aligned, in .bss.
 * ====================================================================== */
.section .bss
.balign 16
__boot_stack_bottom:
    .space 16384
.globl __boot_stack_top
__boot_stack_top:

/* ======================================================================
 * Kernel boot_info — in .data so BSS-clear doesn't overwrite it.
 * ====================================================================== */
.section .data
.balign 8
.globl __kernel_boot_info
__kernel_boot_info:
    .space 256

/* Atomic flag for first-come hart selection (0 = unclaimed) */
.balign 4
__boot_hart_flag:
    .word 0

.section .note.GNU-stack,"",@progbits
