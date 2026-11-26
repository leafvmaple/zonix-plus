#include "block/blk.h"
#include "block/partition.h"
#include "drivers/intr.h"
#include "drivers/pci.h"
#include "drivers/sd/sdhci.h"
#include "drivers/usb/xhci.h"
#include "drivers/virtio_net.h"
#include "drivers/entropy.h"
#include "drivers/virtio_rng.h"
#include "drivers/watchdog.h"
#include "cons/cons.h"
#include "fs/vfs.h"
#include "fs/rootfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/swap.h"
#include "sched/sched.h"
#include "lib/stdio.h"
#include "lib/unistd.h"
#include <kernel/boot_info.h>
#include <kernel/boot_options.h>
#include <kernel/log.h>
#include <asm/arch.h>

#ifdef TEST_MODE
extern int test_run_all(void*);
#endif

// Call C++ global constructors registered in .init_array
extern "C" {
using InitFunction = void (*)();
extern InitFunction __init_array_start[];
extern InitFunction __init_array_end[];
}

static void cxx_init() {
    for (auto* fn = __init_array_start; fn < __init_array_end; fn++) {
        (*fn)();
    }
}

static void run_steps(const InitStep* steps, size_t count) {
    for (size_t i = 0; i < count; i++) {
        klog::debug("init: %s starting\n", steps[i].name);
        int rc = steps[i].fn();
        if (rc != 0) {
            klog::write(steps[i].required ? klog::Level::Error : klog::Level::Warn, "init: %s [FAIL] (rc=%d), %s\n",
                        steps[i].name, rc, steps[i].required ? "halting" : "continuing");
            if (steps[i].required)
                arch_halt();
        } else {
            klog::debug("init: %s completed\n", steps[i].name);
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

static int probe_pci_devices() {
    int rc = pci::probe_drivers();
    // PCI drivers publish disks after blk::init; report the final registry once.
    for (int i = 0; i < BlockManager::device_count(); ++i) {
        auto* device = BlockManager::find_device(i);
        klog::info("%s: %s, %lu MiB\n", device->type == blk::DeviceType::Swap ? "swap device" : "disk", device->name,
                   static_cast<uint64_t>(device->block_count) * BlockDevice::BLOCK_SIZE_BYTES / (1024 * 1024));
    }
    return rc;
}

static const InitStep KERN_STEPS[] = {
    {"early_init", early_init, true},
    {"pmm", pmm::init, true},
    {"vmm", vmm::init, true},
    {"vfs", vfs::init, true},
    {"kmsg", klog::init_device, false},
    {"cons_late", cons::late_init, false},
    {"pci_init", pci::init, false},
    {"blk", blk::init, true},
    {"sdhci", sdhci::init, false},
    {"xhci", xhci::init, false},
    {"pci_reg", pci_registers, false},
    {"virtio_net", virtio_net::init, false},
    {"virtio_rng", virtio_rng::init, false},
    {"entropy", entropy::init, false},
    {"pci_probe", probe_pci_devices, false},
    {"partitions", blk::scan_partitions, false},
#ifdef TEST_MODE
    {"rootfs", rootfs::init, false},
#else
    {"rootfs", rootfs::init, true},
#endif
    {"apps", rootfs::init_apps, false},
    {"swap", swap::init, false},
    {"sched", sched::init, true},
    {"usb", xhci::start_thread, false},
    {"watchdog", watchdog::init, false},
};

// ============================================================================
// Kernel entry point
// ============================================================================

extern "C" [[noreturn]] int kern_init(struct BootInfo* boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        arch_halt();
    }

    cxx_init();

    if (cons::init() != 0) {
        arch_halt();
    }

    run_steps(KERN_STEPS, sizeof(KERN_STEPS) / sizeof(KERN_STEPS[0]));

#ifdef TEST_MODE
    (void)sched::kernel_thread(test_run_all, nullptr);
#else
    // From here programs own the console. Kernel information that arrives
    // later, such as USB devices finishing enumeration, would land in the
    // middle of the shell's line, so only warnings and errors still show;
    // everything stays in the log (/dev/kmsg, /var/log/kernel.log).
    if constexpr (!BOOT_VERBOSE)
        klog::set_console_level(klog::Level::Warn);
#endif

    intr::enable();

    while (true) {
        arch_idle();
        sched::schedule();
    }

    arch_halt();
}
