#pragma once

#include "asm/seg.h"
#include <base/types.h>

/*
 * x86_64 Interrupt/Trap Gate Descriptor (16 bytes)
 * In long mode, gate descriptors are 16 bytes instead of 8.
 */
typedef struct {
    uint16_t gd_off_15_0;    // low 16 bits of offset
    uint16_t gd_ss;          // segment selector
    uint8_t  gd_ist;         // IST index (bits 0-2), reserved bits 3-7
    uint8_t  gd_type_attr;   // type (4 bits) + S(1) + DPL(2) + P(1)
    uint16_t gd_off_31_16;   // bits 16-31 of offset
    uint32_t gd_off_63_32;   // bits 32-63 of offset
    uint32_t gd_rsv;         // reserved, must be zero
} __attribute__((packed)) gate_desc;

// SET_GATE for 64-bit gate descriptors
#define SET_GATE(gate, type, sel, dpl, addr) {                          \
        (gate)->gd_off_15_0   = (uint64_t)(addr) & 0xFFFF;             \
        (gate)->gd_ss         = (sel);                                  \
        (gate)->gd_ist        = 0;                                      \
        (gate)->gd_type_attr  = (0x80 | ((dpl) << 5) | (type));        \
        (gate)->gd_off_31_16  = ((uint64_t)(addr) >> 16) & 0xFFFF;     \
        (gate)->gd_off_63_32  = ((uint64_t)(addr) >> 32) & 0xFFFFFFFF; \
        (gate)->gd_rsv        = 0;                                      \
    }

#define SET_TRAP_GATE(gate, addr) SET_GATE(gate, STS_TG32, GD_KTEXT, DPL_KERNEL, addr)
#define SET_SYS_GATE(gate, addr) SET_GATE(gate, STS_TG32, GD_KTEXT, DPL_USER, addr)

/*
 * x86_64 Segment Descriptor (8 bytes, same layout as 32-bit for code/data)
 * For system descriptors (TSS/LDT) in 64-bit mode, these are 16 bytes.
 */
struct seg_desc {
    unsigned sd_lim_15_0   : 16; // low bits of segment limit
    unsigned sd_base_15_0  : 16; // low bits of segment base address
    unsigned sd_base_23_16 : 8;  // middle bits of segment base address
    unsigned sd_type       : 4;  // segment type (see STS_ constants)
    unsigned sd_s          : 1;  // 0 = system, 1 = application
    unsigned sd_dpl        : 2;  // descriptor Privilege Level
    unsigned sd_p          : 1;  // present
    unsigned sd_lim_19_16  : 4;  // high bits of segment limit
    unsigned sd_avl        : 1;  // unused (available for software use)
    unsigned sd_l          : 1;  // 64-bit code segment (Long mode)
    unsigned sd_db         : 1;  // 0 = 16-bit segment, 1 = 32-bit segment (must be 0 if L=1)
    unsigned sd_g          : 1;  // granularity: limit scaled by 4K when set
    unsigned sd_base_31_24 : 8;  // high bits of segment base address
};

/*
 * x86_64 TSS (Task State Segment) - 104 bytes
 * In long mode, TSS has a different layout than 32-bit.
 * It does NOT contain general-purpose registers.
 */
typedef struct tss_struct {
    uint32_t reserved0;
    uint64_t rsp0;          // Stack pointer for ring 0
    uint64_t rsp1;          // Stack pointer for ring 1
    uint64_t rsp2;          // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist1;          // Interrupt Stack Table 1
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   // I/O Permission Bitmap offset
} __attribute__((packed)) tss_struct;