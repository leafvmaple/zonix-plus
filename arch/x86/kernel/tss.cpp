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
#include <kernel/log.h>

#include <asm/segments.h>
#include "lib/memory.h"
#include "lib/stdio.h"

// GDT lives in head.S (each slot is 8 bytes = uint64_t)
extern uint64_t __gdt[];

namespace {

// The single TSS for this CPU.
class Tss {
public:
    static TssDesc& segment() { return segment_; }
    static uintptr_t fault_stack_top() { return reinterpret_cast<uintptr_t>(fault_stack_) + sizeof(fault_stack_); }

private:
    inline static TssDesc segment_{};
    // A double fault means the current stack is unusable, so it runs here.
    alignas(16) inline static uint8_t fault_stack_[8192]{};
};

}  // namespace

namespace tss {

int init() {
    auto& segment = Tss::segment();
    memset(&segment, 0, sizeof(segment));

    // I/O permission bitmap offset — point past the end of the TSS
    // so that all ports are trapped (no direct user I/O).
    segment.iopb_offset = sizeof(TssDesc);
    static_assert(tss::FAULT_IST == 1);
    segment.ist1 = Tss::fault_stack_top();

    // --- Build the 16-byte TSS descriptor in GDT slots 5 and 6 ---
    uintptr_t base = reinterpret_cast<uintptr_t>(&segment);
    uint32_t limit = sizeof(TssDesc) - 1;

    set_tss(__gdt, SEG_TSS, base, limit);

    // Load the Task Register
    uint16_t tss_sel = GD_TSS;
    __asm__ volatile("ltr %0" : : "r"(tss_sel));

    klog::debug("tss: initialised (base=0x%lx, limit=%d)\n", base, limit);

    return 0;
}

void set_rsp0(uintptr_t rsp0) {
    Tss::segment().rsp0 = rsp0;
}

}  // namespace tss
