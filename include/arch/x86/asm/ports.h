#pragma once

// BIOS Interrupt

#define SMAP 0x534D4150 // 'SMAP'

#define INT_DISK 0x13   // BIOS Disk Services
#define INT_ESI  0x15   // BIOS Extended Services Interface

#define INT_DISK_DL_HDD 0x80
#define INT_DISK_AH_LAB_READ 0x42

#define INT_ESI_AX_E820 0xE820

#define INT_ESI_DESC_SIZE 20
#define INT_ESI_ERROR_CODE 0xFFFF

#define GEN_DAP(sectors,offset,segment,lba) \
    .byte 0x10, 0x00;    \
    .word sectors, offset, segment; \
    .quad lba
