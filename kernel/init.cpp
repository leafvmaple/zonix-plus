#include "block/blk.h"
#include "drivers/pic.h"
#include "drivers/pit.h"
#include "drivers/intr.h"
#include "drivers/fbcons.h"
#include "arch/x86/idt.h"
#include "cons/cons.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/swap.h"
#include "sched/sched.h"
#include "unistd.h"
#include <kernel/bootinfo.h>

static inline _syscall0(int, pause)

// Call C++ global constructors registered in .init_array
extern "C" {
    using ctor_func = void(*)();
    extern ctor_func __init_array_start[];
    extern ctor_func __init_array_end[];
}

static void call_global_ctors() {
    for (auto* fn = __init_array_start; fn < __init_array_end; fn++) {
        (*fn)();
    }
}

extern "C" __attribute__((noreturn)) int kern_init(struct boot_info *boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        goto halt;
    }

    // Call C++ global constructors before anything else
    call_global_ctors();
    
    // Console initialization first (basic driver: CGA + keyboard)
    // This must be first to enable cprintf for other modules
    cons_init();

    // drivers
    pic::init();
    pit::init();

    // arch
    idt_init();

    pmm_init();
    vmm_init();

#ifdef CONFIG_FBCONS
    // Initialize framebuffer console now that VMM can map MMIO
    fbcons::late_init();
#endif

    // Block device initialization (requires VMM for MMIO access)
    blk::init();
    
    // Swap initialization (requires block devices)
    swap_init();

    sched::init();

    intr::enable();

    // Idle loop: PID 0 halts until an interrupt arrives, then reschedules
    while (1) {
        __asm__ volatile("sti; hlt");   // Enable interrupts + halt atomically
        TaskManager::schedule();
    }
halt:
    while (1) __asm__ volatile("hlt");
}
