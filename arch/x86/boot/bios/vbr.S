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

#include <asm/cr.h>
#include <asm/segments.h>
#include <asm/memlayout.h>
#include <asm/e820.h>

#include <asm/drivers/i8042.h>
#include <asm/drivers/i8259.h>

.code16
.globl _start

# FAT16 Boot Sector Structure
_start:
    jmp .Lboot_start
    nop

# BIOS Parameter Block (BPB) - Common for FAT16/FAT32
.Loem_name:           .ascii "ZONIX   "          # 0x03: OEM name
.Lbytes_per_sector:   .word  512                 # 0x0B: Bytes per sector
.Lsectors_per_cluster:.byte  8                   # 0x0D: Sectors per cluster (4KB for FAT32)
.Lreserved_sectors:   .word  32                  # 0x0E: Reserved sectors (32 for FAT32, 1 for FAT16)
.Lnum_fats:           .byte  2                   # 0x10: Number of FAT tables
.Lroot_entries:       .word  0                   # 0x11: Root directory entries (0 for FAT32)
.Ltotal_sectors_16:   .word  0                   # 0x13: Total sectors (0 for FAT32, use .Ltotal_sectors_32)
.Lmedia_type:         .byte  0xF8                # 0x15: Media type (0xF8 = hard disk)
.Lfat_size_16:        .word  0                   # 0x16: Sectors per FAT (0 for FAT32, use .Lfat_size_32)
.Lsectors_per_track:  .word  0x20                # 0x18: Sectors per track
.Lnum_heads:          .word  4                   # 0x1A: Number of heads
.Lhidden_sectors:     .long  0                   # 0x1C: Hidden sectors
.Ltotal_sectors_32:   .long  0                   # 0x20: Total sectors (used by FAT32)

# FAT32 Extended BPB
.Lfat_size_32:        .long  0                   # 0x24: FAT size in sectors (FAT32)
.Lext_flags:          .word  0                   # 0x28: Extended flags
.Lfs_version:         .word  0                   # 0x2A: File system version
.Lroot_cluster:       .long  2                   # 0x2C: Root directory cluster (usually 2)
.Lfs_info:            .word  1                   # 0x30: FSInfo sector number
.Lbackup_boot_sector: .word  6                   # 0x32: Backup boot sector
.Lreserved2:          .space 12                  # 0x34: Reserved
.Ldrive_number:       .byte  0x80                # 0x40: Drive number
.Lreserved3:          .byte  0                   # 0x41: Reserved
.Lboot_signature:     .byte  0x29                # 0x42: Extended boot signature
.Lvolume_id:          .long  0x12345678          # 0x43: Volume ID
.Lvolume_label:       .ascii "ZONIX      "       # 0x47: Volume label
.Lfs_type:            .ascii "FAT32   "          # 0x52: File system type

# Boot code starts here (offset 0x5A for FAT32, 0x3E for FAT16)
.Lboot_start:
    cli
    cld
    xorw %ax    , %ax
    movw %ax    , %ds
    movw %ax    , %es
    movw %ax    , %ss
    movw $0x7C00, %sp
    movb %dl, (.Lboot_drive)
    
    # Probe memory (E820)
    xorw %ax, %ax
    movw %ax, %es
    movl $0, (E820_MEM_BASE)
    xorl %ebx, %ebx
    movw $E820_MEM_DATA, %di
.Lstart_probe:
    movl $E820_QUERY_FUNC, %eax
    movl $E820_DESCRIPTOR_SIZE, %ecx
    movl $E820_SMAP_SIGNATURE, %edx
    int  $E820_BIOS_INT_VECTOR
    jnc  .Lcont                                 # If the CF bit of eflags is 0, it means there are still memory segments to be probed
    movw $E820_COUNT_ERROR_SENTINEL, (E820_MEM_BASE) # Probe has a error, finish
    jmp  .Lfinish_probe
.Lcont:
    addw $E820_DESCRIPTOR_SIZE, %di           # Set the starting address of the mapping address descriptor returned by the next BIOS
    incl (E820_MEM_BASE)                      # Increment the E820 descriptor count
    cmpl $0, %ebx                             # If the ebx returned by INT0x15 is zero, it means the detection is over, otherwise continue to detect
    jnz  .Lstart_probe
.Lfinish_probe:
    # Enable A20
    inb $0x92, %al
    orb $2   , %al
    outb %al , $0x92
    
    # Load GDT
    lgdt .Lgdt_desc

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
    
    # Enable .Lprotected mode
    movl %cr0  , %eax
    orl $CR0_PE, %eax
    movl %eax  , %cr0
    
    # Jump to bootloader at 0x7E00
    ljmp $KERNEL_CS, $.Lprotected

.Lboot_drive:     .byte 0

.code32
.Lprotected:
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
    
    # Push .Lboot_drive as parameter to bootmain
    xorl %eax, %eax
    movb (.Lboot_drive), %al
    pushl %eax
    
    # Jump to bootloader at 0x7E00
    call 0x7E00

.p2align 2
.Lgdt16:
    GEN_SEG_NULL                                    # null seg
    GEN_SEG_DESC(STA_X|STA_R, 0x0, 0xffffffff)      # code seg for bootloader and kernel
    GEN_SEG_DESC(STA_W, 0x0, 0xffffffff)            # data seg for bootloader and kernel
__gdt16_end:

.Lgdt_desc:
    .word __gdt16_end - .Lgdt16 - 1	                # sizeof(gdt) - 1
    .long .Lgdt16                                     # address gdt
