/**
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
    uintptr_t base = reinterpret_cast<uintptr_t>(&s_tss);
    uint32_t limit = sizeof(TssDesc) - 1;

    set_tss(__gdt, SEG_TSS, base, limit);

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
