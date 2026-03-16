/**
 * @file head.S
 * @brief AArch64 kernel entry point for UEFI boot on QEMU virt machine.
 *
 * The UEFI bootloader (BOOTAA64.EFI) loads the kernel ELF at
 * KERNEL_PHYS (0x4008_0000), fills a boot_info structure, calls
 * ExitBootServices, then jumps here with:
 *   - CPU in EL1, MMU ON (UEFI identity map), caches ON
 *   - x0 = physical address of boot_info
 *
 * We disable the MMU (switching away from UEFI page tables),
 * build our own page tables, enable the MMU with higher-half
 * mapping, copy boot_info, clear BSS, and call kern_init.
 *
 * Page table design (4KB granule, 48-bit VA):
 *   PGD (level 0) -> PUD (level 1) with 1GB block entries.
 *   Identity map (TTBR0): PUD[0]=0..1G, PUD[1]=1G..2G
 *   Higher half (TTBR1): PUD[0]=0..1G, PUD[1]=1G..2G
 *
 * After enabling MMU we jump to the higher-half, remove the
 * identity map, clear BSS, copy boot_info, and call kern_init.
 */

/* ---- Page table descriptor bits ---- */
#define PT_VALID        (1 << 0)
#define PT_TABLE        (1 << 1)     /* table descriptor (levels 0-2) */
#define PT_AF           (1 << 10)    /* access flag */
#define PT_ATTR_NORMAL  (0 << 2)     /* MAIR index 0: normal */
#define PT_AP_RW_EL1    (0 << 6)     /* EL1 read/write, EL0 no access */
#define PT_ISH          (3 << 8)     /* inner shareable */

/* PGD -> PUD table descriptor */
#define TABLE_DESC      (PT_VALID | PT_TABLE)

/* 1GB block entry at PUD level (level 1):
 * bit[1]=0 means block (not table), combined with bit[0]=1 for valid */
#define BLOCK_NORMAL    (PT_VALID | PT_AF | PT_ATTR_NORMAL | PT_AP_RW_EL1 | PT_ISH)

/* MAIR: index 0 = 0xFF (normal WriteBack R/W-Allocate) */
#define MAIR_VALUE      0x00000000000000FF

/* TCR: 48-bit VA for both TTBR0 and TTBR1, 4KB granule */
#define TCR_T0SZ        (16)
#define TCR_T1SZ        (16 << 16)
#define TCR_TG0_4K      (0 << 14)
#define TCR_TG1_4K      (2 << 30)
#define TCR_IRGN0       (1 << 8)
#define TCR_ORGN0       (1 << 10)
#define TCR_SH0         (3 << 12)
#define TCR_IRGN1       (1 << 24)
#define TCR_ORGN1       (1 << 26)
#define TCR_SH1         (3 << 28)
#define TCR_IPS_40BIT   (2UL << 32) /* 40-bit PA (1TB), needed for ECAM at 0x40_1000_0000 */
#define TCR_VALUE       (TCR_T0SZ | TCR_T1SZ | TCR_TG0_4K | TCR_TG1_4K | \
                         TCR_IRGN0 | TCR_ORGN0 | TCR_SH0 | \
                         TCR_IRGN1 | TCR_ORGN1 | TCR_SH1 | \
                         TCR_IPS_40BIT)

/* SCTLR bits */
#define SCTLR_M         (1 << 0)
#define SCTLR_C         (1 << 2)
#define SCTLR_I         (1 << 12)

/* Kernel higher-half base address */
#define KERNEL_BASE     0xFFFF000000000000

/* ======================================================================
 * .text.boot — runs with MMU OFF at physical addresses.
 * All symbol references here use adrp/adr (PC-relative), which resolve
 * correctly regardless of VA/PA offset since the relative layout matches.
 * ====================================================================== */
.section .text.boot, "ax"
.globl _start
_start:
    /* ---- Mask all exceptions, save boot_info pointer ---- */
    msr  daifset, #0xF
    mov  x19, x0               /* x19 = boot_info physical address */

    /* ---- Enable FP/SIMD (CPACR_EL1.FPEN = 0b11) ----
     * Without this, variadic C functions (like cprintf) that save
     * q0-q7 in their prologue will trap as SIMD/FP access is undefined.
     */
    mov  x0, #(3 << 20)
    msr  cpacr_el1, x0
    isb

    /* Park non-primary cores */
    mrs  x0, mpidr_el1
    and  x0, x0, #0xFF
    cbnz x0, .Lpark

    /* ---- Disable MMU if UEFI left it on ---- */
    mrs  x0, sctlr_el1
    tst  x0, #SCTLR_M
    b.eq .Lmmu_was_off
    bic  x0, x0, #SCTLR_M         /* clear M (MMU) */
    bic  x0, x0, #SCTLR_C         /* clear C (data cache) */
    msr  sctlr_el1, x0
    isb
    /* Invalidate all TLBs and instruction cache */
    tlbi vmalle1
    ic   iallu
    dsb  sy
    isb
.Lmmu_was_off:

    /* ---- Zero page tables (6 pages = 24 KB) ---- */
    adrp x0, __boot_pgd_low
    mov  x1, #(6 * 4096)
1:  stp  xzr, xzr, [x0], #16
    subs x1, x1, #16
    b.ne 1b

    /* ---- Identity map (TTBR0): VA == PA ----
     * PGD[0] -> PUD
     * PUD[0] = 1GB block: phys 0x0000_0000 .. 0x3FFF_FFFF  (devices)
     * PUD[1] = 1GB block: phys 0x4000_0000 .. 0x7FFF_FFFF  (RAM/kernel)
     * (Identity map uses 1GB blocks — fine since it's removed after boot.)
     */
    adrp x0, __boot_pgd_low
    adrp x1, __boot_pud_low
    orr  x2, x1, #TABLE_DESC
    str  x2, [x0]                    /* PGD[0] -> PUD */

    ldr  x2, =BLOCK_NORMAL
    orr  x3, xzr, x2                /* phys 0x00000000 | flags */
    str  x3, [x1, #0]               /* PUD[0] */
    mov  x4, #0x40000000
    orr  x3, x4, x2                 /* phys 0x40000000 | flags */
    str  x3, [x1, #8]               /* PUD[1] */

    /* ---- Higher-half map (TTBR1): KERNEL_BASE -> phys 0 ----
     * Uses 2MB block entries at PMD level (not 1GB blocks at PUD level)
     * so the kernel page table walker (get_pte) can split them into
     * 4KB pages when needed.
     *
     * PGD[0]  -> PUD
     * PUD[0]  -> PMD0  (table entry, covers phys 0..1GB)
     * PUD[1]  -> PMD1  (table entry, covers phys 1G..2G)
     * PMD0[i] = (i * 2MB) | BLOCK_NORMAL     for i = 0..511
     * PMD1[i] = (1GB + i * 2MB) | BLOCK_NORMAL  for i = 0..511
     */
    adrp x0, __boot_pgd_high
    adrp x1, __boot_pud_high
    orr  x2, x1, #TABLE_DESC
    str  x2, [x0]                    /* PGD[0] -> PUD */

    /* PUD[0] -> PMD0 (table descriptor) */
    adrp x2, __boot_pmd_high0
    orr  x2, x2, #TABLE_DESC
    str  x2, [x1, #0]               /* PUD[0] */

    /* PUD[1] -> PMD1 (table descriptor) */
    adrp x2, __boot_pmd_high1
    orr  x2, x2, #TABLE_DESC
    str  x2, [x1, #8]               /* PUD[1] */

    /* Fill PMD0: 512 × 2MB block entries covering phys 0..1G */
    adrp x0, __boot_pmd_high0
    ldr  x2, =BLOCK_NORMAL
    mov  x3, xzr                    /* pa = 0 */
    mov  x4, #512
.Lfill_pmd0:
    orr  x5, x3, x2
    str  x5, [x0], #8
    add  x3, x3, #(2 * 1024 * 1024) /* pa += 2MB */
    subs x4, x4, #1
    b.ne .Lfill_pmd0

    /* Fill PMD1: 512 × 2MB block entries covering phys 1G..2G */
    adrp x0, __boot_pmd_high1
    mov  x3, #0x40000000             /* pa = 1GB */
    mov  x4, #512
.Lfill_pmd1:
    orr  x5, x3, x2
    str  x5, [x0], #8
    add  x3, x3, #(2 * 1024 * 1024) /* pa += 2MB */
    subs x4, x4, #1
    b.ne .Lfill_pmd1

    /* ---- Configure translation control ---- */
    ldr  x0, =MAIR_VALUE
    msr  mair_el1, x0

    ldr  x0, =TCR_VALUE
    msr  tcr_el1, x0

    adrp x0, __boot_pgd_low
    msr  ttbr0_el1, x0
    adrp x0, __boot_pgd_high
    msr  ttbr1_el1, x0
    isb

    /* ---- Enable MMU + caches ---- */
    mrs  x0, sctlr_el1
    orr  x0, x0, #SCTLR_M
    orr  x0, x0, #SCTLR_C
    orr  x0, x0, #SCTLR_I
    msr  sctlr_el1, x0
    isb

    /* ---- Jump to higher-half virtual address ----
     * adr gives the current (physical) address of .Lhigh.
     * Adding KERNEL_BASE yields the TTBR1 virtual address.
     */
    ldr  x1, =KERNEL_BASE
    adr  x0, .Lhigh
    add  x0, x0, x1
    br   x0

.Lhigh:
    /* Now running at higher-half (TTBR1) addresses. */

    /* Remove identity mapping: zero PGD_LOW[0] */
    adrp x0, __boot_pgd_low
    str  xzr, [x0]
    tlbi vmalle1
    dsb  sy
    isb

    /* ---- Set up kernel stack ---- */
    ldr  x0, =__boot_stack_top
    mov  sp, x0

    /* ---- Clear BSS ---- */
    ldr  x0, =__bss_start
    ldr  x1, =__bss_end
1:  cmp  x0, x1
    b.ge 2f
    stp  xzr, xzr, [x0], #16
    b    1b
2:

    /* ---- Copy boot_info from bootloader to kernel storage ----
     * x19 = physical address of boot_info (saved at entry).
     * Now running at higher-half, so access via KERNEL_BASE + phys.
     * __kernel_boot_info is already a higher-half symbol.
     *
     * struct boot_info is 102 bytes; copy 128 bytes (16 × stp) to be safe.
     */
    ldr  x1, =KERNEL_BASE
    add  x0, x19, x1            /* src = boot_info phys → higher-half VA */
    ldr  x1, =__kernel_boot_info /* dst */
    /* Copy 128 bytes (8 pairs of 16 bytes) */
    ldp  x2, x3, [x0, #0]
    stp  x2, x3, [x1, #0]
    ldp  x2, x3, [x0, #16]
    stp  x2, x3, [x1, #16]
    ldp  x2, x3, [x0, #32]
    stp  x2, x3, [x1, #32]
    ldp  x2, x3, [x0, #48]
    stp  x2, x3, [x1, #48]
    ldp  x2, x3, [x0, #64]
    stp  x2, x3, [x1, #64]
    ldp  x2, x3, [x0, #80]
    stp  x2, x3, [x1, #80]
    ldp  x2, x3, [x0, #96]
    stp  x2, x3, [x1, #96]
    ldp  x2, x3, [x0, #112]
    stp  x2, x3, [x1, #112]

    /* Pass boot_info pointer to kern_init */
    ldr  x0, =__kernel_boot_info
    bl   kern_init

    /* Should not return */
.Lpark:
    wfe
    b    .Lpark

/* ======================================================================
 * Page tables — 6 × 4KB, page-aligned, in .data.pgdir
 *
 * TTBR0 (identity, removed after boot): PGD_low, PUD_low (1GB blocks)
 * TTBR1 (higher-half, permanent):       PGD_high, PUD_high, PMD0, PMD1
 *                                        (2MB blocks at PMD level)
 * ====================================================================== */
.section .data.pgdir, "aw", @progbits

.balign 4096
__boot_pgd_low:     .space 4096
__boot_pud_low:     .space 4096
.globl __boot_pgd_high
__boot_pgd_high:    .space 4096
__boot_pud_high:    .space 4096
__boot_pmd_high0:   .space 4096
__boot_pmd_high1:   .space 4096

/* ======================================================================
 * Boot stack — 16 KB, 16-byte aligned, in .bss
 * ====================================================================== */
.section .bss
.balign 16
__boot_stack_bottom:
    .space 16384
__boot_stack_top:

/* ======================================================================
 * Kernel boot_info — in .data so BSS clear doesn't overwrite it
 * ====================================================================== */
.section .data
.balign 8
.globl __kernel_boot_info
__kernel_boot_info:
    .space 256

.section .note.GNU-stack,"",@progbits
