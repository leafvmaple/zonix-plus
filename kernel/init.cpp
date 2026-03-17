#include "block/blk.h"
#include "drivers/intr.h"
#include "drivers/pci.h"
#include "cons/cons.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/swap.h"
#include "sched/sched.h"
#include "lib/stdio.h"
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

static void run_steps(const InitStep* steps, size_t count) {
    for (size_t i = 0; i < count; i++) {
        int rc = steps[i].fn();
        cprintf("  %-12s", steps[i].name);
        if (rc != 0) {
            cprintf(" [FAIL] (rc=%d)\n", rc);
            if (steps[i].required)
                arch_halt();
        } else {
            cprintf(" [OK]\n");
        }
    }
}

static int early_init() {
    size_t arch_count = 0;
    const InitStep* arch_steps = arch_early_steps(&arch_count);
    run_steps(arch_steps, arch_count);

    return 0;
}

static int pci_registers() {
    size_t arch_count = 0;
    const InitStep* arch_steps = arch_pci_steps(&arch_count);
    run_steps(arch_steps, arch_count);

    return 0;
}

static const InitStep KERN_STEPS[] = {
    {"early_init", early_init, true},  {"pmm", pmm::init, true},
    {"vmm", vmm::init, true},          {"cons_late", cons::late_init, false},
    {"pci_init", pci::init, false},    {"blk", blk::init, true},
    {"pci_reg", pci_registers, false}, {"pci_probe", pci::probe_drivers, false},
    {"swap", swap::init, false},       {"sched", sched::init, true},
};

// ============================================================================
// Kernel entry point
// ============================================================================

extern "C" __attribute__((noreturn)) int kern_init(struct boot_info* boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        arch_halt();
    }

    cxx_init();

    if (cons::init() != 0) {
        arch_halt();
    }

    run_steps(KERN_STEPS, sizeof(KERN_STEPS) / sizeof(KERN_STEPS[0]));

    intr::enable();

    while (true) {
        arch_idle();
        sched::schedule();
    }

    arch_halt();
}
