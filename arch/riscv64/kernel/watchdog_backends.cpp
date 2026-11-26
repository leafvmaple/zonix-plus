/**
 * @file watchdog_backends.cpp
 * @brief RISC-V platform watchdogs.
 *
 * QEMU virt has none the kernel drives. The JH7110 watchdog resets the whole
 * chip, so the VisionFive 2 boots again from its firmware.
 */

#include "drivers/watchdog.h"
#include "drivers/intr.h"
#include "drivers/mmio.h"
#include "drivers/timer.h"
#include <kernel/log.h>

#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>

namespace {

// JH7110 watchdog registers. Writes take effect only while the lock holds the
// unlock key. The counter runs down from the load value; the first expiry
// raises its interrupt and a second one without a clear resets the chip, so
// the reset comes after twice the loaded period. Clearing the interrupt also
// reloads the counter.
constexpr uintptr_t WDT_LOAD = 0x000;
constexpr uintptr_t WDT_VALUE = 0x004;
constexpr uintptr_t WDT_CONTROL = 0x008;
constexpr uintptr_t WDT_INTERRUPT_CLEAR = 0x00C;
constexpr uintptr_t WDT_LOCK = 0xC00;
constexpr uint32_t WDT_ENABLE = 1U << 0;
constexpr uint32_t WDT_RESET_ENABLE = 1U << 1;
constexpr uint32_t WDT_CLEAR = 1;
constexpr uint32_t WDT_UNLOCK_KEY = 0x1ACCE551;
constexpr uint32_t WDT_LOCK_KEY = ~WDT_UNLOCK_KEY;

// System clock and reset generator: one register per clock with its gate in
// the top bit, then the reset assert bitmaps.
constexpr uint32_t CLOCK_GATE_ENABLE = 1U << 31;
constexpr uintptr_t RESET_ASSERT_OFFSET = 0x2F8;

// The counter must run between these rates before the reset is enabled. A
// counter that does not count down means the registers are not what this
// driver expects, and the watchdog is left off rather than risk resets.
constexpr uint64_t MEASURE_US = 10000;
constexpr uint64_t MIN_COUNTER_HZ = 1000000;
constexpr uint64_t MAX_COUNTER_HZ = 200000000;
constexpr uint32_t MAX_LOAD = 0xFFFFFFFF;

class Jh7110Watchdog {
public:
    static Error start(uint32_t seconds) {
        enable_clocks();
        uint64_t rate = measure();
        if (rate < MIN_COUNTER_HZ || rate > MAX_COUNTER_HZ) {
            stop();
            klog::warn("watchdog: counter runs at %lu Hz, not arming\n", rate);
            return Error::NoDevice;
        }
        // The chip resets after two loaded periods.
        uint64_t load = rate * seconds / 2;
        load_ = static_cast<uint32_t>(load > MAX_LOAD ? MAX_LOAD : load);
        intr::Guard guard;
        unlock();
        mmio::write32(base(), WDT_CONTROL, 0);
        mmio::write32(base(), WDT_INTERRUPT_CLEAR, WDT_CLEAR);
        mmio::write32(base(), WDT_LOAD, load_);
        mmio::write32(base(), WDT_CONTROL, WDT_ENABLE | WDT_RESET_ENABLE);
        lock();
        klog::debug("watchdog: counter at %lu Hz, load %u\n", rate, load_);
        return Error::None;
    }

    static void feed() {
        unlock();
        mmio::write32(base(), WDT_INTERRUPT_CLEAR, WDT_CLEAR);
        mmio::write32(base(), WDT_LOAD, load_);
        lock();
    }

    static void stop() {
        intr::Guard guard;
        unlock();
        mmio::write32(base(), WDT_CONTROL, 0);
        lock();
    }

private:
    static uintptr_t base() { return BOARD_WATCHDOG_PHYS + KERNEL_BASE; }
    static void unlock() { mmio::write32(base(), WDT_LOCK, WDT_UNLOCK_KEY); }
    static void lock() { mmio::write32(base(), WDT_LOCK, WDT_LOCK_KEY); }

    static void enable_clocks() {
        constexpr uint32_t clocks[] = {BOARD_WATCHDOG_APB_CLOCK, BOARD_WATCHDOG_CORE_CLOCK};
        constexpr uint32_t resets[] = {BOARD_WATCHDOG_APB_RESET, BOARD_WATCHDOG_CORE_RESET};
        const uintptr_t crg = BOARD_SYSCRG_PHYS + KERNEL_BASE;
        for (uint32_t clock : clocks) {
            const uintptr_t offset = static_cast<uintptr_t>(clock) * sizeof(uint32_t);
            mmio::write32(crg, offset, mmio::read32(crg, offset) | CLOCK_GATE_ENABLE);
        }
        for (uint32_t reset : resets) {
            const uintptr_t offset = RESET_ASSERT_OFFSET + reset / 32 * sizeof(uint32_t);
            mmio::write32(crg, offset, mmio::read32(crg, offset) & ~(1U << (reset % 32)));
        }
        arch_mb();
    }

    // Lets the counter run from its largest value with the reset output off
    // and returns how fast it counts down.
    static uint64_t measure() {
        {
            intr::Guard guard;
            unlock();
            mmio::write32(base(), WDT_CONTROL, 0);
            mmio::write32(base(), WDT_INTERRUPT_CLEAR, WDT_CLEAR);
            mmio::write32(base(), WDT_LOAD, MAX_LOAD);
            mmio::write32(base(), WDT_CONTROL, WDT_ENABLE);
            lock();
        }
        const uint64_t ticks = timer::TIMER_FREQ_HZ * MEASURE_US / 1000000;
        const uint64_t started = timer::now();
        const uint32_t first = mmio::read32(base(), WDT_VALUE);
        while (timer::now() - started < ticks)
            arch_spin_hint();
        const uint32_t second = mmio::read32(base(), WDT_VALUE);
        const uint64_t elapsed = timer::now() - started;
        if (second >= first || elapsed == 0)
            return 0;
        return static_cast<uint64_t>(first - second) * timer::TIMER_FREQ_HZ / elapsed;
    }

    inline static uint32_t load_{};
};

}  // namespace

namespace watchdog {

Error start_platform(uint32_t seconds) {
    if constexpr (BOARD_JH7110_WATCHDOG) {
        return Jh7110Watchdog::start(seconds);
    } else {
        static_cast<void>(seconds);
        return Error::NotSupported;
    }
}

void feed_platform() {
    if constexpr (BOARD_JH7110_WATCHDOG)
        Jh7110Watchdog::feed();
}

void stop_platform() {
    if constexpr (BOARD_JH7110_WATCHDOG)
        Jh7110Watchdog::stop();
}

}  // namespace watchdog
