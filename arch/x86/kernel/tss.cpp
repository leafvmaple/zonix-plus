/**
 * x86_64 TSS initialisation and runtime RSP0 updates.
 *
 * GDT layout (from head.S):
 *   Slot 0  = NULL
 *   Slot 1  = KERNEL_CS   (64-bit code, DPL 0)
 *   Slot 2  = KERNEL_DS   (data, DPL 0)
 *   Slot 3  = USER_CS     (64-bit code, DPL 3)
 *   Slot 4  = USER_DS     (data, DPL 3)
 *   Slot 5  = TSS low     ← we fill this
 *   Slot 6  = TSS high    ← and this (16-byte system descriptor)
 */

#include "tss.h"

#include <asm/seg.h>
#include <asm/segments.h>
#include "lib/memory.h"
#include "lib/stdio.h"

// GDT lives in head.S (each slot is 8 bytes = uint64_t)
extern uint64_t __gdt[];

// The single, static TSS for this CPU.
static TssDesc s_tss;

namespace tss {

int init() {
    memset(&s_tss, 0, sizeof(s_tss));

    // I/O permission bitmap offset — point past the end of the TSS
    // so that all ports are trapped (no direct user I/O).
    s_tss.iopb_offset = sizeof(TssDesc);

    // --- Build the 16-byte TSS descriptor in GDT slots 5 and 6 ---
    uint64_t base = reinterpret_cast<uint64_t>(&s_tss);
    uint32_t limit = sizeof(TssDesc) - 1;

    // Low 8 bytes (slot 5):
    //   bits  0-15  limit[15:0]
    //   bits 16-31  base[15:0]
    //   bits 32-39  base[23:16]
    //   bits 40-43  type = 0x9 (Available 64-bit TSS)
    //   bit  44     S = 0 (system descriptor)
    //   bits 45-46  DPL = 0
    //   bit  47     P = 1
    //   bits 48-51  limit[19:16]
    //   bits 52-55  AVL=0, L=0, D/B=0, G=0
    //   bits 56-63  base[31:24]
    uint64_t low = 0;
    low |= (uint64_t)(limit & 0xFFFF);             // limit[15:0]
    low |= (uint64_t)(base & 0xFFFF) << 16;        // base[15:0]
    low |= (uint64_t)((base >> 16) & 0xFF) << 32;  // base[23:16]
    low |= (uint64_t)(STS_T32A) << 40;             // type = Available TSS
    low |= (uint64_t)(1) << 47;                    // P = 1
    low |= (uint64_t)((limit >> 16) & 0xF) << 48;  // limit[19:16]
    low |= (uint64_t)((base >> 24) & 0xFF) << 56;  // base[31:24]

    // High 8 bytes (slot 6):
    //   bits  0-31  base[63:32]
    //   bits 32-63  reserved (zero)
    uint64_t high = (base >> 32) & 0xFFFFFFFF;

    __gdt[SEG_TSS] = low;
    __gdt[SEG_TSS + 1] = high;

    // Load the Task Register
    uint16_t tss_sel = GD_TSS;
    __asm__ volatile("ltr %0" : : "r"(tss_sel));

    cprintf("tss: initialised (base=0x%lx, limit=%d)\n", base, limit);

    return ARCH_INIT_OK;
}

void set_rsp0(uintptr_t rsp0) {
    s_tss.rsp0 = rsp0;
}

}  // namespace tss
