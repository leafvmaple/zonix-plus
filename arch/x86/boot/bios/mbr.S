#include <base/mbr.h>
# MBR (Master Boot Record)
# Located at the first sector of disk (LBA 0), size 512 bytes
# Function: Find active partition and load VBR (Volume Boot Record)

.code16                                 # 16-bit real mode
.globl _start
_start:
    cli                                 # Disable interrupts
    cld                                 # Clear direction flag
    
    # Setup segment registers
    xorw %ax, %ax
    movw %ax, %ds                       # DS = 0
    movw %ax, %es                       # ES = 0
    movw %ax, %ss                       # SS = 0
    movw $0x7C00, %sp                   # Stack pointer below 0x7C00

    # Relocate MBR from 0x7C00 to 0x0600 (standard practice)
    movw $0x7C00, %si                   # Source address
    movw $0x0600, %di                   # Destination address
    movw $0x100 , %cx                   # Copy 256 words (512 bytes)
    rep movsw                           # Copy
    
    # Jump to .Lrelocated code
    ljmp $0x0, $.Lrelocated

.Lrelocated:
    # Save boot drive number (BIOS passes in DL)
    movb %dl, .Lboot_drive

    # Display boot message
    movw $.Lmsg_mbr, %si
    call .Lprint_string

    # Find active partition (search partition table for bootable partition)
    movw $.Lpartition_table, %si   # Partition table start address
    movw $4, %cx                 # 4 partition entries

.Lcheck_partition:
    cmpb $PARTITION_BOOTABLE, (%si)     # Check boot flag (0x80 = active partition)
    je .Lload_vbr                         # Found active partition
    addw $16, %si                       # Move to next partition entry (16 bytes each)
    loop .Lcheck_partition

    # No active partition found
    movw $.Lmsg_no_active, %si
    call .Lprint_string
    jmp .Lhalt

.Lload_vbr:
    # SI now points to active partition entry
    # Get partition starting LBA (offset 8-11, 4 bytes)
    movl 8(%si), %eax                   # LBA starting sector
    
    # Read VBR & Bootloader (sectors 1-3) to 0x7C00 & 0x7E00
    movw $0x7C00, %bx                   # Target address for read
    movb $8     , %cl                   # Read 8 sector
    call .Lread_disk

    # Check VBR signature (last two bytes should be 0xAA55)
    cmpw $MBR_SIGNATURE, 0x7DFE
    jne  .Linvalid_vbr
    
    movb .Lboot_drive, %dl                # Restore drive number to DL
    
    # Far jump to VBR at 0x0000:0x7C00
    ljmp $0x0, $0x7C00

.Linvalid_vbr:
    movw $.Lmsg_invalid_vbr, %si
    call .Lprint_string
    jmp .Lhalt

.Lhalt:
    hlt
    jmp .Lhalt

# ============================================
# Function: .Lprint_string
# Input: SI = string address (null-terminated)
# ============================================
.Lprint_string:
    pusha
    movb $0x0E, %ah                     # BIOS teletype function
.print_loop:
    lodsb                               # Load [SI] to AL, SI++
    cmpb $0   , %al
    je   .print_done
    int  $0x10                          # BIOS interrupt to display character
    jmp  .print_loop
.print_done:
    popa
    ret

# ============================================
# Function: .Lread_disk
# Input: EAX = LBA sector number, BX = target address, CL = sector count
# ============================================
.Lread_disk:
    # Update the Disk Address Packet (DAP) with actual values
    # Only need to update non-zero fields (packet already initialized)
    movw $.Ldap, %si
    movw %cx , 2(%si)                   # sector count
    movw %bx , 4(%si)                   # buffer offset
    movl %eax, 8(%si)                   # LBA starting sector

    # Call extended read AH=42h with DS:SI -> .Ldap
    movb $0x42     , %ah
    movb .Lboot_drive, %dl
    int  $0x13
    jc   .Ldisk_error

    # Return to caller (registers restored earlier)
    ret

.Ldisk_error:
    movw $.Lmsg_disk_error, %si
    call .Lprint_string
    jmp .Lhalt

# ============================================
# Data area
# ============================================
.Lboot_drive:
    .byte 0

# LBA read data packet
.Ldap:
    .byte 0x10                          # Packet size
    .byte 0                             # Reserved
    .word 1                             # Sector count
    .word 0                             # Buffer offset
    .word 0                             # Buffer segment
    .quad 0                             # LBA address

# Message strings
.Lmsg_mbr:
    .asciz "MBR Loading...\r\n"

.Lmsg_no_active:
    .asciz "No active partition!\r\n"

.Lmsg_invalid_vbr:
    .asciz "Invalid VBR signature!\r\n"

.Lmsg_disk_error:
    .asciz "Disk read error!\r\n"

# Pad to 446 bytes (space before partition table)
. = _start + MBR_PARTITION_OFFSET

# ============================================
# Partition table (4 entries, 16 bytes each)
# ============================================
.Lpartition_table:
    # Partition 1: Active partition, starts from sector 1
    .byte PARTITION_BOOTABLE            # Boot flag (0x80 = bootable)
    .byte 0x00, 0x01, 0x00              # Starting CHS
    .byte PART_TYPE_LINUX               # Partition type (0x83 = Linux)
    .byte 0xFF, 0xFF, 0xFF              # Ending CHS
    .long 1                             # LBA starting sector (starts from sector 1)
    .long 8063                          # Sector count (about 4MB)

    # Partitions 2-4: Unused
    .fill 16, 1, 0
    .fill 16, 1, 0
    .fill 16, 1, 0

# ============================================
# MBR signature (0xAA55)
# ============================================
. = _start + 510
    .byte 0x55
    .byte 0xAA
