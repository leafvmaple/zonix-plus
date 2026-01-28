# vbr.S - Volume Boot Record for zonix
# FAT32 VBR that loads KERNEL.SYS from FAT filesystem
#
# This is the partition boot sector (VBR), not the MBR.
# It is loaded by MBR at 0x7C00 after partition table check.
#
# Functionality:
#   1. Parse FAT BPB (BIOS Parameter Block) - supports FAT32
#   2. Search for KERNEL.SYS in root directory
#   3. Load kernel by following FAT chain
#   4. Detect memory using E820
#   5. Enable A20 line
#   6. Switch to protected mode
#   7. Jump to kernel at 0x10000

#include <arch/x86/asm/cr.h>
#include <arch/x86/asm/seg.h>
#include <arch/x86/asm/ports.h>

#include <arch/x86/drivers/i8042.h>
#include <arch/x86/drivers/i8259.h>

.code16
.globl _start

# FAT16 Boot Sector Structure
_start:
    jmp boot_start
    nop

# BIOS Parameter Block (BPB) - Common for FAT16/FAT32
oem_name:           .ascii "ZONIX   "          # 0x03: OEM name
bytes_per_sector:   .word  512                 # 0x0B: Bytes per sector
sectors_per_cluster:.byte  8                   # 0x0D: Sectors per cluster (4KB for FAT32)
reserved_sectors:   .word  32                  # 0x0E: Reserved sectors (32 for FAT32, 1 for FAT16)
num_fats:           .byte  2                   # 0x10: Number of FAT tables
root_entries:       .word  0                   # 0x11: Root directory entries (0 for FAT32)
total_sectors_16:   .word  0                   # 0x13: Total sectors (0 for FAT32, use total_sectors_32)
media_type:         .byte  0xF8                # 0x15: Media type (0xF8 = hard disk)
fat_size_16:        .word  0                   # 0x16: Sectors per FAT (0 for FAT32, use fat_size_32)
sectors_per_track:  .word  0x20                # 0x18: Sectors per track
num_heads:          .word  4                   # 0x1A: Number of heads
hidden_sectors:     .long  0                   # 0x1C: Hidden sectors
total_sectors_32:   .long  0                   # 0x20: Total sectors (used by FAT32)

# FAT32 Extended BPB
fat_size_32:        .long  0                   # 0x24: FAT size in sectors (FAT32)
ext_flags:          .word  0                   # 0x28: Extended flags
fs_version:         .word  0                   # 0x2A: File system version
root_cluster:       .long  2                   # 0x2C: Root directory cluster (usually 2)
fs_info:            .word  1                   # 0x30: FSInfo sector number
backup_boot_sector: .word  6                   # 0x32: Backup boot sector
reserved2:          .space 12                  # 0x34: Reserved
drive_number:       .byte  0x80                # 0x40: Drive number
reserved3:          .byte  0                   # 0x41: Reserved
boot_signature:     .byte  0x29                # 0x42: Extended boot signature
volume_id:          .long  0x12345678          # 0x43: Volume ID
volume_label:       .ascii "ZONIX      "       # 0x47: Volume label
fs_type:            .ascii "FAT32   "          # 0x52: File system type

# Boot code starts here (offset 0x5A for FAT32, 0x3E for FAT16)
boot_start:
    cli
    cld
    xorw %ax    , %ax
    movw %ax    , %ds
    movw %ax    , %es
    movw %ax    , %ss
    movw $0x7C00, %sp
    movb %dl, (boot_drive)
    
    # Probe memory (E820)
    xorw %ax, %ax
    movw %ax, %es
    movl $0, (E820_MEM_BASE)
    xorl %ebx, %ebx
    movw $E820_MEM_DATA, %di
start_probe:
    movl $0xE820, %eax
    movl $INT_ESI_DESC_SIZE, %ecx
    movl $SMAP, %edx
    int  $0x15
    jnc  cont                                 # If the CF bit of eflags is 0, it means there are still memory segments to be probed
    movw $0xFFFF, (E820_MEM_BASE)             # Probe has a error, finish
    jmp  finish_probe
cont:
    addw $INT_ESI_DESC_SIZE, %di              # Set the starting address of the mapping address descriptor returned by the next BIOS
    incl (E820_MEM_BASE)                      # Increment the member variable nr_map of struct e820map
    cmpl $0, %ebx                             # If the ebx returned by INT0x15 is zero, it means the detection is over, otherwise continue to detect
    jnz  start_probe
finish_probe:
    # Enable A20
    inb $0x92, %al
    orb $2   , %al
    outb %al , $0x92
    
    # Load GDT
    lgdt gdt_desc

    # PIC Init
# ICW1: Init PIC, Set Enable ICW4
    mov $ICW1_ICW4 | ICW1_INIT, %al
    out %al, $PIC1_CMD
    out %al, $PIC2_CMD

# ICW2: Set Interrupt Address
    mov $IRQ_OFFSET, %al        # Master PIC Interrupt [RQ0, RQ8) is [0x20, 0x28)
    out %al, $PIC1_IMR

    mov $IRQ_OFFSET + 8, %al    # Master PIC Interrupt [RQ8, RQ16) is [0x28, 0x30)
    out %al, $PIC2_IMR

# ICW3:
    mov $BIT_SLAVE, %al                 # Slave PIC connect in IRQ_SLAVE
    out %al, $PIC1_IMR

    mov $IRQ_SLAVE, %al                 # connect to Master PIC's IRQ2(IRQ_SLAVE)
    out %al, $PIC2_IMR

# ICW4: Set Work Mode
    mov $ICW4_8086 | ICW4_AUTO, %al
    out %al, $PIC1_IMR
    out %al, $PIC2_IMR

    mov $0xFF, %al
    out %al, $PIC1_IMR
    out %al, $PIC2_IMR
    
    # Enable protected mode
    movl %cr0  , %eax
    orl $CR0_PE, %eax
    movl %eax  , %cr0
    
    # Jump to bootloader at 0x7E00
    ljmp $KERNEL_CS, $protected

boot_drive:     .byte 0

.code32
protected:
    # Set up 32-bit data segments
    movw $KERNEL_DS, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    # Set up stack
    movl $0x7000, %esp
    movl $0x0, %ebp
    
    # Push boot_drive as parameter to bootmain
    xorl %eax, %eax
    movb (boot_drive), %al
    pushl %eax
    
    # Jump to bootloader at 0x7E00
    call 0x7E00

.p2align 2
gdt16:
    GEN_SEG_NULL                                    # null seg
    GEN_SEG_DESC(STA_X|STA_R, 0x0, 0xffffffff)      # code seg for bootloader and kernel
    GEN_SEG_DESC(STA_W, 0x0, 0xffffffff)            # data seg for bootloader and kernel
__gdt16_end:

gdt_desc:
    .word __gdt16_end - gdt16 - 1	                # sizeof(gdt) - 1
    .long gdt16                                     # address gdt
