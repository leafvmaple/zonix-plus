// =============================================================================
// enter_long_mode.S - 32-bit to 64-bit long mode transition
//
// Called from C as: enter_long_mode(kernel_entry_phys, boot_info_ptr)
//   %esp+4  = kernel_entry_phys  (physical address of kernel entry point)
//   %esp+8  = boot_info_ptr      (pointer to struct boot_info)
//
// This function NEVER returns. It:
//   1. Enables PAE in CR4
//   2. Loads PML4 into CR3
//   3. Enables Long Mode in EFER MSR
//   4. Enables Paging in CR0 (activating long mode)
//   5. Loads 64-bit GDT
//   6. Far-jumps to 64-bit code
//   7. In 64-bit mode: sets segments, loads boot_info into %rdi, jumps to kernel
// =============================================================================

// Memory layout constants (must match bootload.c)
#define BOOT_PML4_ADDR       0x1000
#define BOOT_GDT64_ADDR      0x6000
#define BOOT_GDTDESC64_ADDR  0x6040

// MSR addresses
#define MSR_EFER  0xC0000080
#define EFER_LME  0x100

// CR0 bits
#define CR0_PG    0x80000000
#define CR0_WP    0x00010000

// CR4 bits
#define CR4_PAE   0x20

.code32
.section .text

.globl enter_long_mode
.type  enter_long_mode, @function

enter_long_mode:
    // Retrieve parameters from stack (cdecl calling convention)
    movl    4(%esp), %ebx           // ebx = kernel_entry_phys
    movl    8(%esp), %esi           // esi = boot_info pointer

    // 1. Enable PAE (CR4.PAE = bit 5)
    movl    %cr4, %eax
    orl     $CR4_PAE, %eax
    movl    %eax, %cr4

    // 2. Load PML4 into CR3
    movl    $BOOT_PML4_ADDR, %eax
    movl    %eax, %cr3

    // 3. Enable Long Mode (EFER.LME = bit 8)
    movl    $MSR_EFER, %ecx
    rdmsr
    orl     $EFER_LME, %eax
    wrmsr

    // 4. Enable Paging (CR0.PG) and Write Protect (CR0.WP)
    movl    %cr0, %eax
    orl     $(CR0_PG | CR0_WP), %eax
    movl    %eax, %cr0

    // 5. Load 64-bit GDT
    lgdt    (BOOT_GDTDESC64_ADDR)

    // 6. Far jump to 64-bit code segment (GD_KTEXT = 0x08)
    ljmp    $0x08, $long_mode_entry

// =============================================================================
// 64-bit code begins here
// =============================================================================
.code64
long_mode_entry:
    // Set up 64-bit data segments (GD_KDATA = 0x10)
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %ax, %ax
    movw    %ax, %fs
    movw    %ax, %gs

    // Set up a temporary stack
    movq    $0x7000, %rsp

    // Load boot_info pointer into %rdi (System V x86_64 ABI, first argument)
    // %esi was saved earlier (32-bit), zero-extend to 64-bit
    movl    %esi, %edi

    // Jump to kernel entry point (physical address in %ebx, zero-extended)
    movl    %ebx, %eax
    jmp     *%rax

// Switch back to 32-bit mode for the assembler (in case more code follows)
.code32
