#include <asm/pg.h>
#include <asm/seg.h>
#include <asm/cr.h>

#define REALLOC(x) ((x) - KERNEL_BASE)
#define BOOT_INFO_SIZE 128  // sizeof(struct boot_info), must be >= actual size

.globl _main, __gdt, __idt, __boot_pml4, __kernel_boot_info

/*
 * Entry point for the 64-bit kernel.
 *
 * We arrive here from EITHER:
 *   - BIOS bootloader: already switched to 64-bit long mode with identity mapping
 *   - UEFI bootloader: already in 64-bit long mode with identity mapping
 *
 * Both bootloaders must:
 *   - Be in 64-bit long mode with paging enabled
 *   - Have an identity mapping covering at least 0..kernel_end
 *   - Pass boot_info pointer in %rdi (x86_64 System V ABI)
 *   - Have a 64-bit GDT with code/data segments loaded
 *
 * We need to:
 *   1. Save boot_info
 *   2. Set up identity-mapped + higher-half page tables (4-level)
 *   3. Load our own PML4 into CR3
 *   4. Jump to higher-half address
 *   5. Remove identity mapping
 *   6. Call kern_init
 */

.code64
_main:
    # Disable interrupts
    cli

    # ---------------------------------------------------------------
    # Save boot_info: copy from bootloader's buffer to kernel's own copy
    # We are running at physical address, symbols are linked at virtual,
    # so we must use REALLOC() for all symbol references in this phase.
    # ---------------------------------------------------------------
    movq %rdi, %rsi                          # Source: boot_info from bootloader
    movq $REALLOC(__kernel_boot_info), %rdi  # Dest: kernel's boot_info copy (phys addr)
    movq $BOOT_INFO_SIZE, %rcx
    rep movsb

    # ---------------------------------------------------------------
    # Set up 4-level page tables for identity + higher-half mapping
    # We map the first 1GB using 2MB pages (PTE_PS) for simplicity
    # Identity map:   0x00000000_00000000 -> 0x00000000_3FFFFFFF
    # Higher-half:    0xFFFFFFFF_80000000 -> 0xFFFFFFFF_BFFFFFFF  (= phys 0..1GB)
    # ---------------------------------------------------------------

    # Clear page table area (5 pages = 5 * 4096 bytes)
    movq $REALLOC(__boot_pml4), %rdi
    xorq %rax, %rax
    movq $(5 * 4096 / 8), %rcx    # 5 pages in qwords
    rep stosq

    # PML4[0] -> boot_pdpt_low (identity map)
    movq $REALLOC(__boot_pdpt_low), %rax
    orq $(PTE_P | PTE_W), %rax
    movq $REALLOC(__boot_pml4), %rdi
    movq %rax, (%rdi)                        # PML4[0]

    # PML4[511] -> boot_pdpt_high (higher-half map for 0xFFFFFFFF_80000000)
    movq $REALLOC(__boot_pdpt_high), %rax
    orq $(PTE_P | PTE_W), %rax
    movq %rax, 511 * 8(%rdi)                 # PML4[511]

    # PDPT_low[0] -> boot_pd_low (maps 0..1GB)
    movq $REALLOC(__boot_pd_low), %rax
    orq $(PTE_P | PTE_W), %rax
    movq $REALLOC(__boot_pdpt_low), %rdi
    movq %rax, (%rdi)                        # PDPT_low[0]

    # PDPT_high[510] -> boot_pd_high (maps 0xFFFFFFFF_80000000..+1GB)
    # 0xFFFFFFFF_80000000 >> 30 & 0x1FF = 510
    movq $REALLOC(__boot_pd_high), %rax
    orq $(PTE_P | PTE_W), %rax
    movq $REALLOC(__boot_pdpt_high), %rdi
    movq %rax, 510 * 8(%rdi)                 # PDPT_high[510]

    # Fill PD_low and PD_high with 512 x 2MB identity-mapped pages
    # PD entry = (i * 2MB) | PTE_P | PTE_W | PTE_PS
    movq $REALLOC(__boot_pd_low), %rdi
    movq $REALLOC(__boot_pd_high), %rsi
    xorq %rax, %rax             # physical address starts at 0
    movq $512, %rcx
1:
    movq %rax, %rdx
    orq $(PTE_P | PTE_W | PTE_PS), %rdx
    movq %rdx, (%rdi)
    movq %rdx, (%rsi)
    addq $0x200000, %rax    # next 2MB
    addq $8, %rdi
    addq $8, %rsi
    decq %rcx
    jnz 1b

    # ---------------------------------------------------------------
    # Ensure PAE is enabled (should already be set, but be safe)
    # ---------------------------------------------------------------
    movq %cr4, %rax
    orq $CR4_PAE, %rax
    movq %rax, %cr4

    # ---------------------------------------------------------------
    # Set up a temporary stack at a safe low-memory address.
    # This is critical for the UEFI path: UEFI firmware's stack may be
    # above 1GB, but our new page tables only identity-map 0..1GB.
    # After CR3 is loaded below, any stack access to high addresses
    # would page-fault. (The BIOS path already has RSP=0x7000.)
    # ---------------------------------------------------------------
    movq $0x7000, %rsp

    # ---------------------------------------------------------------
    # Load our PML4 into CR3 (replaces bootloader's page tables)
    # This works because both old and new page tables identity-map low memory
    # ---------------------------------------------------------------
    movq $REALLOC(__boot_pml4), %rax
    movq %rax, %cr3

    # ---------------------------------------------------------------
    # Load 64-bit GDT (using physical address, computed via REALLOC)
    # ---------------------------------------------------------------
    movq $REALLOC(__gdtdesc_phys), %rax
    lgdt (%rax)

    # Reload CS with a far return
    pushq $GD_KTEXT
    movabsq $REALLOC(.Lreload_cs_phys), %rax
    pushq %rax
    lretq
.Lreload_cs_phys:

    # Set up 64-bit data segments
    movw $GD_KDATA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    # ---------------------------------------------------------------
    # Jump to higher-half address
    # We have both identity map and higher-half map active, so we can
    # jump to the virtual address and continue execution there.
    # ---------------------------------------------------------------
    movabsq $_start64_high, %rax
    jmp *%rax

_start64_high:
    # Now running at higher-half virtual address
    # RIP-relative addressing now works correctly with virtual symbols

    # Reload GDT with higher-half virtual addresses
    lgdt __gdtdesc(%rip)

    # Reload CS with higher-half GDT
    pushq $GD_KTEXT
    leaq .Lreload_cs_high(%rip), %rax
    pushq %rax
    lretq
.Lreload_cs_high:

    # Remove identity mapping (PML4[0] = 0)
    movq $0, __boot_pml4(%rip)
    # Flush TLB
    movq %cr3, %rax
    movq %rax, %cr3

    # Clear BSS section
    movq $__bss_start, %rdi
    movq $__bss_end, %rcx
    subq %rdi, %rcx
    xorb %al, %al
    rep stosb

    # Set up kernel stack
    movq STACK_START(%rip), %rsp

    # Load IDT
    lidt __idtdesc(%rip)

    # Call kern_init with kernel's boot_info pointer
    leaq __kernel_boot_info(%rip), %rdi    # x86_64 calling convention: first arg in %rdi
    call kern_init

    # If kern_init returns (it shouldn't), halt.
_spin:
    hlt
    jmp _spin


# ===================================================================
# Data section
# ===================================================================

.align 16
__gdt:
    GEN_SEG_NULL                    # 0: NULL descriptor
    GEN_SEG_CODE64                  # 1: 64-bit kernel code segment
    GEN_SEG_DATA64                  # 2: kernel data segment
    GEN_SEG_NULL                    # 3: placeholder (user code)
    GEN_SEG_NULL                    # 4: placeholder (user data)
    GEN_SEG_NULL                    # 5: TSS low (set up later in C)
    GEN_SEG_NULL                    # 6: TSS high (16-byte TSS descriptor)

    .fill 249, 8, 0                # space for future descriptors
__gdt_end:

.align 16
__idt:
    .fill 256 * 2, 8, 0             # 64-bit IDT: each entry is 16 bytes (256 * 2 * 8)
__idt_end:

.align 8
# GDT descriptor with physical addresses (used during identity-mapped phase)
__gdtdesc_phys:
    .word __gdt_end - __gdt - 1
    .quad REALLOC(__gdt)

.align 8
# 64-bit GDT descriptor (used after entering higher-half, with virtual addresses)
__gdtdesc:
    .word __gdt_end - __gdt - 1
    .quad __gdt

__idtdesc:
    .word __idt_end - __idt - 1    # 256 * 16 - 1
    .quad __idt

# ===================================================================
# Page tables (each 4KB-aligned)
# ===================================================================
.section .data.pgdir, "aw", @progbits

.align PG_SIZE
__boot_pml4:
    .space PG_SIZE

.align PG_SIZE
__boot_pdpt_low:
    .space PG_SIZE

.align PG_SIZE
__boot_pdpt_high:
    .space PG_SIZE

.align PG_SIZE
__boot_pd_low:
    .space PG_SIZE

.align PG_SIZE
__boot_pd_high:
    .space PG_SIZE

# ===================================================================
# Kernel boot info copy (in .data so BSS clear does not overwrite it)
# ===================================================================
.section .data
.align 8
__kernel_boot_info:
    .fill BOOT_INFO_SIZE, 1, 0

.section .note.GNU-stack,"",@progbits