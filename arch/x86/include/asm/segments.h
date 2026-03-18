#pragma once

#define STA_X 0x8  // Executable segment
#define STA_E 0x4  // Expand down (non-executable segments)
#define STA_C 0x4  // Conforming code segment (executable only)
#define STA_W 0x2  // Writeable (non-executable segments)
#define STA_R 0x2  // Readable (executable segments)
#define STA_A 0x1  // Accessed

#define STS_T16A 0x1  // Available 16-bit TSS
#define STS_LDT  0x2  // Local Descriptor Table
#define STS_T16B 0x3  // Busy 16-bit TSS
#define STS_CG16 0x4  // 16-bit Call Gate
#define STS_TG   0x5  // Task Gate / Coum Transmitions
#define STS_IG16 0x6  // 16-bit Interrupt Gate
#define STS_TG16 0x7  // 16-bit Trap Gate
#define STS_T32A 0x9  // Available 32/64-bit TSS
#define STS_T32B 0xB  // Busy 32/64-bit TSS
#define STS_CG32 0xC  // 32-bit Call Gate
#define STS_IG32 0xE  // 32/64-bit Interrupt Gate
#define STS_TG32 0xF  // 32/64-bit Trap Gate

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
#define GD_TSS   ((SEG_TSS) << 3)    // task segment selector

#define KERNEL_CS ((GD_KTEXT) | DPL_KERNEL)
#define KERNEL_DS ((GD_KDATA) | DPL_KERNEL)
#define USER_CS   ((GD_UTEXT) | DPL_USER)
#define USER_DS   ((GD_UDATA) | DPL_USER)

#define GEN_SEG_NULL \
    .word 0, 0;      \
    .byte 0, 0, 0, 0

/* 64-bit code segment: L=1, D=0, P=1, DPL=0, S=1, Type=Execute/Read */
#define GEN_SEG_CODE64    \
    .word 0xFFFF, 0x0000; \
    .byte 0x00, 0x9A, 0xAF, 0x00

/* 64-bit data segment: P=1, DPL=0, S=1, Type=Read/Write */
#define GEN_SEG_DATA64    \
    .word 0xFFFF, 0x0000; \
    .byte 0x00, 0x92, 0xCF, 0x00

/* 64-bit user code segment: L=1, D=0, P=1, DPL=3, S=1, Type=Execute/Read */
#define GEN_SEG_UCODE64   \
    .word 0xFFFF, 0x0000; \
    .byte 0x00, 0xFA, 0xAF, 0x00

/* 64-bit user data segment: P=1, DPL=3, S=1, Type=Read/Write */
#define GEN_SEG_UDATA64   \
    .word 0xFFFF, 0x0000; \
    .byte 0x00, 0xF2, 0xCF, 0x00

#define GEN_SEG_DESC(type, base, lim)                 \
    .word(((lim) >> 12) & 0xffff), ((base) & 0xffff); \
    .byte(((base) >> 16) & 0xff), (0x90 | (type)), (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#ifndef __ASSEMBLY__

#include <base/types.h>

struct GateDesc {
    uint16_t gd_off_15_0;   // low 16 bits of offset
    uint16_t gd_ss;         // segment selector
    uint8_t gd_ist;         // IST index (bits 0-2), reserved bits 3-7
    uint8_t gd_type_attr;   // type (4 bits) + S(1) + DPL(2) + P(1)
    uint16_t gd_off_31_16;  // bits 16-31 of offset
    uint32_t gd_off_63_32;  // bits 32-63 of offset
    uint32_t gd_rsv;        // reserved, must be zero
} __attribute__((packed));

inline void set_gate(GateDesc* gate, uint8_t type, uint16_t sel, uint8_t dpl, uintptr_t addr) {
    gate->gd_off_15_0 = static_cast<uint16_t>(addr & 0xFFFF);
    gate->gd_ss = sel;
    gate->gd_ist = 0;
    gate->gd_type_attr = static_cast<uint8_t>(0x80 | (dpl << 5) | type);
    gate->gd_off_31_16 = static_cast<uint16_t>((addr >> 16) & 0xFFFF);
    gate->gd_off_63_32 = static_cast<uint32_t>((addr >> 32) & 0xFFFFFFFF);
    gate->gd_rsv = 0;
}

template<uint8_t Type, uint16_t Sel, uint8_t Dpl>
inline void set_gate(GateDesc* gate, uintptr_t addr) {
    static_assert((Type & 0xF) == Type, "gate type must fit in 4 bits");
    static_assert((Dpl & 0x3) == Dpl, "DPL must fit in 2 bits");
    set_gate(gate, Type, Sel, Dpl, addr);
}

inline void set_trap_gate(GateDesc* gate, uintptr_t addr) {
    set_gate<STS_TG32, GD_KTEXT, DPL_KERNEL>(gate, addr);
}

inline void set_sys_gate(GateDesc* gate, uintptr_t addr) {
    set_gate<STS_TG32, GD_KTEXT, DPL_USER>(gate, addr);
}

inline void set_tss(uint64_t* gdt, uint16_t seg, uintptr_t base, uint32_t limit) {
    uint64_t low = 0;
    low |= static_cast<uint64_t>(limit & 0xFFFF);             // limit[15:0]
    low |= static_cast<uint64_t>(base & 0xFFFF) << 16;        // base[15:0]
    low |= static_cast<uint64_t>((base >> 16) & 0xFF) << 32;  // base[23:16]
    low |= static_cast<uint64_t>(STS_T32A) << 40;             // type = Available TSS
    low |= static_cast<uint64_t>(1) << 47;                    // P = 1
    low |= static_cast<uint64_t>((limit >> 16) & 0xF) << 48;  // limit[19:16]
    low |= static_cast<uint64_t>((base >> 24) & 0xFF) << 56;  // base[31:24]

    // bits 0-31: base[63:32], bits 32-63: reserved (zero)
    const uint64_t high = (static_cast<uint64_t>(base) >> 32) & 0xFFFFFFFFULL;

    gdt[seg] = low;
    gdt[seg + 1] = high;
}

struct SegmentDesc {
    unsigned sd_lim_15_0 : 16;   // low bits of segment limit
    unsigned sd_base_15_0 : 16;  // low bits of segment base address
    unsigned sd_base_23_16 : 8;  // middle bits of segment base address
    unsigned sd_type : 4;        // segment type (see STS_ constants)
    unsigned sd_s : 1;           // 0 = system, 1 = application
    unsigned sd_dpl : 2;         // descriptor Privilege Level
    unsigned sd_p : 1;           // present
    unsigned sd_lim_19_16 : 4;   // high bits of segment limit
    unsigned sd_avl : 1;         // unused (available for software use)
    unsigned sd_l : 1;           // 64-bit code segment (Long mode)
    unsigned sd_db : 1;          // 0 = 16-bit segment, 1 = 32-bit segment (must be 0 if L=1)
    unsigned sd_g : 1;           // granularity: limit scaled by 4K when set
    unsigned sd_base_31_24 : 8;  // high bits of segment base address
};

struct TssDesc {
    uint32_t reserved0;
    uint64_t rsp0;  // Stack pointer for ring 0
    uint64_t rsp1;  // Stack pointer for ring 1
    uint64_t rsp2;  // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist1;  // Interrupt Stack Table 1
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;  // I/O Permission Bitmap offset
} __attribute__((packed));

#endif /* !__ASSEMBLY__ */