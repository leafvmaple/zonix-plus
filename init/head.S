#include <arch/x86/asm/pg.h>
#include <arch/x86/asm/seg.h>
#include <arch/x86/asm/cr.h>

#define REALLOC(x) ((x) - KERNEL_BASE)
#define BOOT_INFO_SIZE 128  // sizeof(struct boot_info), must be >= actual size

.globl _main, __gdt, __idt, __boot_pml4, __kernel_boot_info

/*
 * Entry point for the 64-bit kernel.
 * We arrive here from the bootloader already in 32-bit protected mode.
 * We need to:
 *   1. Save boot_info
 *   2. Set up identity-mapped + higher-half page tables (4-level)
 *   3. Enable PAE, set PML4 in CR3
 *   4. Enable long mode via EFER MSR
 *   5. Enable paging
 *   6. Far-jump to 64-bit code
 */

.code32
_main:
    # Disable interrupts
    cli

    # Save boot_info pointer (passed by bootloader on stack)
    movl 4(%esp), %esi              # Source: boot_info from bootloader
    movl $REALLOC(__kernel_boot_info), %edi  # Dest: kernel's boot_info copy (phys addr)
    movl $BOOT_INFO_SIZE, %ecx
    rep movsb

    # ---------------------------------------------------------------
    # Set up 4-level page tables for identity + higher-half mapping
    # We map the first 1GB using 2MB pages (PTE_PS) for simplicity
    # Identity map:   0x00000000_00000000 -> 0x00000000_3FFFFFFF
    # Higher-half:    0xFFFFFFFF_80000000 -> 0xFFFFFFFF_BFFFFFFF  (= phys 0..1GB)
    # ---------------------------------------------------------------

    # Clear page table area
    movl $REALLOC(__boot_pml4), %edi
    xorl %eax, %eax
    movl $(5 * 4096 / 4), %ecx    # 5 pages (PML4, 2xPDPT, 2xPD) in dwords
    rep stosl

    # PML4[0] -> boot_pdpt_low (identity map)
    movl $REALLOC(__boot_pdpt_low), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, REALLOC(__boot_pml4) + 0 * 8     # PML4[0]

    # PML4[511] -> boot_pdpt_high (higher-half map for 0xFFFFFFFF_80000000)
    movl $REALLOC(__boot_pdpt_high), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, REALLOC(__boot_pml4) + 511 * 8   # PML4[511]

    # PDPT_low[0] -> boot_pd_low (maps 0..1GB)
    movl $REALLOC(__boot_pd_low), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, REALLOC(__boot_pdpt_low) + 0 * 8

    # PDPT_high[510] -> boot_pd_high (maps 0xFFFFFFFF_80000000..+1GB)
    # 0xFFFFFFFF_80000000 >> 30 & 0x1FF = 510
    movl $REALLOC(__boot_pd_high), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, REALLOC(__boot_pdpt_high) + 510 * 8

    # Fill PD_low and PD_high with 512 x 2MB identity-mapped pages
    # PD entry = (i * 2MB) | PTE_P | PTE_W | PTE_PS
    movl $REALLOC(__boot_pd_low), %edi
    movl $REALLOC(__boot_pd_high), %esi
    movl $0, %eax           # physical address starts at 0
    movl $512, %ecx
1:
    movl %eax, %edx
    orl $(PTE_P | PTE_W | PTE_PS), %edx
    movl %edx, (%edi)       # low dword
    movl $0, 4(%edi)         # high dword = 0 (below 4GB)
    movl %edx, (%esi)       # same for high mapping
    movl $0, 4(%esi)
    addl $0x200000, %eax    # next 2MB
    addl $8, %edi
    addl $8, %esi
    decl %ecx
    jnz 1b

    # ---------------------------------------------------------------
    # Enable PAE (CR4.PAE = 1)
    # ---------------------------------------------------------------
    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4

    # ---------------------------------------------------------------
    # Load PML4 into CR3
    # ---------------------------------------------------------------
    movl $REALLOC(__boot_pml4), %eax
    movl %eax, %cr3

    # ---------------------------------------------------------------
    # Enable Long Mode (EFER.LME = 1)
    # ---------------------------------------------------------------
    movl $MSR_EFER, %ecx
    rdmsr
    orl $EFER_LME, %eax
    wrmsr

    # ---------------------------------------------------------------
    # Enable Paging (CR0.PG = 1) - this activates long mode
    # ---------------------------------------------------------------
    movl %cr0, %eax
    orl $(CR0_PG | CR0_WP), %eax
    movl %eax, %cr0

    # ---------------------------------------------------------------
    # Load 64-bit GDT and far-jump to 64-bit code
    # ---------------------------------------------------------------
    lgdt REALLOC(__gdtdesc32)
    ljmp $GD_KTEXT, $REALLOC(_start64)

# ===================================================================
# 64-bit code starts here
# ===================================================================
.code64
_start64:
    # Set up 64-bit data segments
    movw $GD_KDATA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    # Jump to higher-half address (we were running at identity-mapped address)
    movabsq $_start64_high, %rax
    jmp *%rax

_start64_high:
    # Now running at higher-half virtual address

    # Reload GDT with higher-half virtual addresses
    lgdt __gdtdesc(%rip)

    # Remove identity mapping (PML4[0] = 0)
    movq $0, __boot_pml4
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

.align 4
# 32-bit GDT descriptor (used before paging/long mode, with physical addresses)
__gdtdesc32:
    .word __gdt_end - __gdt - 1
    .long REALLOC(__gdt)             # 4-byte physical address for 32-bit lgdt

.align 4
# 64-bit GDT descriptor (used after entering long mode, with virtual addresses)
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
# Kernel boot info copy (in BSS)
# ===================================================================
.align 8
__kernel_boot_info:
    .space BOOT_INFO_SIZE

.section .note.GNU-stack,"",@progbits