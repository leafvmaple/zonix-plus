#pragma once

#include "asm/seg.h"

typedef struct {
    unsigned gd_off_15_0  : 16;  // low 16 bits of offset in segment
    unsigned gd_ss        : 16;  // segment selector
    unsigned gd_args      : 5;   // # args, 0 for interrupt/trap gates
    unsigned gd_rsv1      : 3;   // reserved(should be zero I guess)
    unsigned gd_type      : 4;   // type(STS_{TG,IG32,TG32})
    unsigned gd_s         : 1;   // must be 0 (system)
    unsigned gd_dpl       : 2;   // descriptor(meaning new) privilege level
    unsigned gd_p         : 1;   // Present
    unsigned gd_off_31_16 : 16;  // high bits of offset in segment, always 0
} gate_desc;

#ifdef _ASM_
// gate_addr under 0x10000, so gd_off_31_16 always 0
#define SET_GATE(gate_addr, type, dpl, addr) \
__asm__ (               \
    "movw %%dx , %%ax;" \
    "movw %0   , %%dx;" \
    "movl %%eax, %1;"   \
    "movl %%edx, %2;"   \
    : \
    : "i" ((short) (0x8000 + (dpl << 13) + (type << 8))), "o" (*((char *) (gate_addr))), "o" (*(4+(char *) (gate_addr))), "d" ((char *) (addr)))

#else
// Too slow, but useful
#define SET_GATE(gate, type, sel, dpl, addr) {                                                     \
        (gate)->gd_off_15_0  = (uint32_t)(addr)&0xFFFF;   \
        (gate)->gd_ss        = (sel);                     \
        (gate)->gd_args      = 0;                         \
        (gate)->gd_rsv1      = 0;                         \
        (gate)->gd_type      = type;                      \
        (gate)->gd_s         = 0;                         \
        (gate)->gd_dpl       = (dpl);                     \
        (gate)->gd_p         = 1;                         \
        (gate)->gd_off_31_16 = (uint32_t)(addr) >> 16;    \
    }
#endif

#define SET_TRAP_GATE(gate, addr) SET_GATE(gate, STS_TG32, GD_KTEXT, DPL_KERNEL, addr)
#define SET_SYS_GATE(gate, addr) SET_GATE(gate, STS_TG32, GD_KTEXT, DPL_USER, addr)

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
    unsigned sd_rsv1       : 1;  // reserved
    unsigned sd_db         : 1;  // 0 = 16-bit segment, 1 = 32-bit segment
    unsigned sd_g          : 1;  // granularity: limit scaled by 4K when set
    unsigned sd_base_31_24 : 8;  // high bits of segment base address
};

#define SET_TSS_SEG(seg, type, base, lim, dpl) {       \
        (seg)->sd_lim_15_0   = (lim) & 0xffff;         \
        (seg)->sd_base_15_0  = (base) & 0xffff;        \
        (seg)->sd_base_23_16 = ((base) >> 16) & 0xff;  \
        (seg)->sd_type       = type;                   \
        (seg)->sd_dpl        = dpl;                    \
        (seg)->sd_p          = 1;                      \
        (seg)->sd_lim_19_16  = (unsigned)(lim) >> 16;  \
        (seg)->sd_db         = 1;                      \
        (seg)->sd_base_31_24 = (unsigned)(base) >> 24; \
    }

typedef struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
} tss_struct;