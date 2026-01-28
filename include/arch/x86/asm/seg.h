#pragma once

#define STA_X 0x8  // Executable segment
#define STA_E 0x4  // Expand down (non-executable segments)
#define STA_C 0x4  // Conforming code segment (executable only)
#define STA_W 0x2  // Writeable (non-executable segments)
#define STA_R 0x2  // Readable (executable segments)
#define STA_A 0x1  // Accessed

/* System segment type bits */
#define STS_T16A 0x1  // Available 16-bit TSS
#define STS_LDT  0x2  // Local Descriptor Table
#define STS_T16B 0x3  // Busy 16-bit TSS
#define STS_CG16 0x4  // 16-bit Call Gate
#define STS_TG   0x5  // Task Gate / Coum Transmitions
#define STS_IG16 0x6  // 16-bit Interrupt Gate
#define STS_TG16 0x7  // 16-bit Trap Gate
#define STS_T32A 0x9  // Available 32-bit TSS
#define STS_T32B 0xB  // Busy 32-bit TSS
#define STS_CG32 0xC  // 32-bit Call Gate
#define STS_IG32 0xE  // 32-bit Interrupt Gate
#define STS_TG32 0xF  // 32-bit Trap Gate

#define DPL_KERNEL 0
#define DPL_USER   3

#define SEG_KTEXT 1
#define SEG_KDATA 2
#define SEG_UTEXT 3
#define SEG_UDATA 4
#define SEG_TSS   5

#define GD_KTEXT ((SEG_KTEXT) << 3)  // kernel text
#define GD_KDATA ((SEG_KDATA) << 3)  // kernel data
#define GD_UTEXT ((SEG_UTEXT) << 3)  // user text
#define GD_UDATA ((SEG_UDATA) << 3)  // user data
#define GD_TSS   ((SEG_TSS)   << 3)  // task segment selector

#define KERNEL_CS ((GD_KTEXT) | DPL_KERNEL)
#define KERNEL_DS ((GD_KDATA) | DPL_KERNEL)
#define USER_CS   ((GD_UTEXT) | DPL_USER)
#define USER_DS   ((GD_UDATA) | DPL_USER)

#define GATE_DESC(type, dpl) (0x8000 + (dpl << 13) + (type << 8))

/* Normal segment */
#define GEN_SEG_NULL                                            \
    .word 0, 0;                                                 \
    .byte 0, 0, 0, 0

#define GEN_SEG_DESC(type,base,lim)                             \
    .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);          \
    .byte (((base) >> 16) & 0xff), (0x90 | (type)), (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)


// Memory Layout

#define KERNEL_BASE 0xC0000000
#define KERNEL_HEADER 0x10000
#define KERNEL_MEM_SIZE 0x38000000

#define E820_MEM_BASE 0x7000
#define E820_MEM_DATA (E820_MEM_BASE + 4)

#define E820_RAM 1
#define E820_RESERVED 2
#define E820_ACPI 3
#define E820_NVS 4

#define VPT 0xFAC00000

// Logical Memory Layout
/*
      KERNEL_BASE ---------> +---------------------------------+ 0xC0000000
*/


// Physical Memory Management
/* *
      DISK2_END -----------> +---------------------------------+ 0x01FF0000   32MB
      KERNEL_BASE ---------> +---------------------------------+ 0x00100000    1MB
      KERNEL_HEADER -------> +---------------------------------+ 0x00010000   64KB
*     E820_MEM_DATA -------> +---------------------------------+ 0x00008004
*     E820_MEM_BASE -------> +---------------------------------+ 0x00008000   32KB
*                            |              BIOS IVT           | --/--
*     DISK1_BEGIN ---------> +---------------------------------+ 0x00000000
 * */