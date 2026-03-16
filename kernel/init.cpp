#include "block/blk.h"
#include "drivers/intr.h"
#include "cons/cons.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/swap.h"
#include "sched/sched.h"
#include "lib/unistd.h"
#include <kernel/bootinfo.h>
#include <asm/arch.h>

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

static void run_arch_early_steps() {
    size_t count = 0;
    const ArchEarlyStep* steps = arch_early_steps(&count);
    if (steps == nullptr) {
        arch_halt();
    }

    for (size_t i = 0; i < count; i++) {
        int rc = steps[i].fn();
        if (rc != 0 && steps[i].required) {
            arch_halt();
        }
    }
}

extern "C" __attribute__((noreturn)) int kern_init(struct boot_info* boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        goto halt;
    }

    cxx_init();

    cons::init();

    run_arch_early_steps();

    pmm::init();
    vmm::init();

    cons::late_init();
    blk::init();
    swap::init();

    sched::init();

    intr::enable();

    while (true) {
        arch_idle();
        sched::schedule();
    }
halt:
    arch_halt();
}
