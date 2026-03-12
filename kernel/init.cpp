#include "block/blk.h"
#include "drivers/pic.h"
#include "drivers/pit.h"
#include "drivers/intr.h"
#include "drivers/fbcons.h"
#include "idt.h"
#include "tss.h"
#include "cons/cons.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/swap.h"
#include "sched/sched.h"
#include "lib/unistd.h"
#include <kernel/bootinfo.h>

// Call C++ global constructors registered in .init_array
extern "C" {
using ctor_func = void (*)();
extern ctor_func __init_array_start[];
extern ctor_func __init_array_end[];
}

static void cxx_init() {
    for (auto* fn = __init_array_start; fn < __init_array_end; fn++) {
        (*fn)();
    }
}

extern "C" __attribute__((noreturn)) int kern_init(struct boot_info* boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        goto halt;
    }

    // C++ global constructors (must run before any other init)
    cxx_init();

    // Console initialization first (basic driver: CGA + keyboard)
    // This must be first to enable cprintf for other modules
    cons::init();

    // drivers
    pic::init();
    pit::init();

    // arch
    idt::init();
    tss::init();

    pmm::init();
    vmm::init();

    // Initialize framebuffer console now that VMM can map MMIO
    fbcons::late_init();

    // Block device initialization (requires VMM for MMIO access)
    blk::init();

    // Swap initialization (requires block devices)
    swap::init();

    sched::init();

    intr::enable();

    // Idle loop: PID 0 halts until an interrupt arrives, then reschedules
    while (true) {
        __asm__ volatile("sti; hlt");  // Enable interrupts + halt atomically
        sched::schedule();
    }
halt:
    while (true)
        __asm__ volatile("hlt");
}
