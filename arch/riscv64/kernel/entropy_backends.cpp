/**
 * @file entropy_backends.cpp
 * @brief RISC-V platform random number generators.
 *
 * QEMU virt has none; the VirtIO entropy device serves it. The JH7110 has a
 * true random number generator that the kernel polls: each command produces
 * 256 bits, read from eight result registers.
 */

#include "drivers/entropy.h"
#include "drivers/mmio.h"
#include "drivers/timer.h"
#include "lib/mutex.h"
#include "sched/sched.h"

#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>

namespace {

// JH7110 TRNG registers. Commands go to TRNG_COMMAND; the controller reports
// their completion in TRNG_EVENTS, whose bits are cleared by writing them
// back. Its interrupt line is never enabled at the PLIC: the kernel polls.
constexpr uintptr_t TRNG_COMMAND = 0x00;
constexpr uintptr_t TRNG_STATUS = 0x04;
constexpr uintptr_t TRNG_MODE = 0x08;
constexpr uintptr_t TRNG_EVENT_ENABLE = 0x10;
constexpr uintptr_t TRNG_EVENTS = 0x14;
constexpr uintptr_t TRNG_RESULT = 0x20;
constexpr uintptr_t TRNG_AUTO_REQUESTS = 0x60;
constexpr uintptr_t TRNG_AUTO_AGE = 0x64;

constexpr uint32_t TRNG_COMMAND_GENERATE = 0x1;
constexpr uint32_t TRNG_COMMAND_RESEED = 0x2;
constexpr uint32_t TRNG_STATUS_GENERATING = 1U << 30;
constexpr uint32_t TRNG_STATUS_SEEDING = 1U << 31;
constexpr uint32_t TRNG_MODE_256_BITS = 1U << 3;
constexpr uint32_t TRNG_EVENT_RESULT_READY = 1U << 0;
constexpr uint32_t TRNG_EVENT_SEED_DONE = 1U << 1;
constexpr uint32_t TRNG_EVENT_LFSR_LOCKUP = 1U << 4;
constexpr uint32_t TRNG_EVENT_GLOBAL_ENABLE = 1U << 31;
constexpr uint32_t TRNG_EVENTS_ALL =
    TRNG_EVENT_RESULT_READY | TRNG_EVENT_SEED_DONE | TRNG_EVENT_LFSR_LOCKUP | TRNG_EVENT_GLOBAL_ENABLE;
constexpr size_t TRNG_RESULT_WORDS = 8;

// Commands finish within microseconds; these bound a controller that stopped.
constexpr uint64_t IDLE_TIMEOUT_US = 100000;
constexpr uint64_t COMMAND_TIMEOUT_US = 10000;

// Clock and reset generators: one register per clock with its gate in the
// top bit; the STG block's resets are one assert bitmap.
constexpr uint32_t CLOCK_GATE_ENABLE = 1U << 31;
constexpr uintptr_t STG_RESET_ASSERT = 0x74;

class Jh7110Trng {
public:
    static Error start() {
        enable_clocks();
        const uintptr_t trng = base();
        // Automatic reseeding stays off; the kernel reseeds at start and
        // after a lockup.
        mmio::write32(trng, TRNG_AUTO_AGE, 0);
        mmio::write32(trng, TRNG_AUTO_REQUESTS, 0);
        mmio::write32(trng, TRNG_EVENTS, mmio::read32(trng, TRNG_EVENTS));
        mmio::write32(trng, TRNG_EVENT_ENABLE, TRNG_EVENTS_ALL);
        mmio::write32(trng, TRNG_MODE, mmio::read32(trng, TRNG_MODE) | TRNG_MODE_256_BITS);
        return run(TRNG_COMMAND_RESEED, TRNG_EVENT_SEED_DONE);
    }

    static Result<size_t> read(void* output, size_t size) {
        TRY(mutex_.lock_interruptible());
        sys::lock_guard<Mutex> lock(mutex_, sys::adopt_lock);
        auto* bytes = static_cast<uint8_t*>(output);
        size_t filled = 0;
        while (filled < size) {
            ENSURE(!sched::current()->termination_requested(), Error::Interrupted);
            uint32_t words[TRNG_RESULT_WORDS];
            TRY(generate(words));
            size_t count = size - filled < sizeof(words) ? size - filled : sizeof(words);
            memcpy(bytes + filled, words, count);
            memset(words, 0, sizeof(words));
            filled += count;
        }
        return filled;
    }

private:
    static uintptr_t base() { return BOARD_TRNG_PHYS + KERNEL_BASE; }

    static uint64_t now_us() { return timer::now() * 1000000 / timer::TIMER_FREQ_HZ; }

    static Error generate(uint32_t (&words)[TRNG_RESULT_WORDS]) {
        const uintptr_t trng = base();
        // A single-event upset locks the generator's LFSR until it reseeds.
        if ((mmio::read32(trng, TRNG_EVENTS) & TRNG_EVENT_LFSR_LOCKUP) != 0) {
            mmio::write32(trng, TRNG_EVENTS, TRNG_EVENT_LFSR_LOCKUP);
            TRY(run(TRNG_COMMAND_RESEED, TRNG_EVENT_SEED_DONE));
        }
        TRY(run(TRNG_COMMAND_GENERATE, TRNG_EVENT_RESULT_READY));
        for (size_t i = 0; i < TRNG_RESULT_WORDS; ++i)
            words[i] = mmio::read32(trng, TRNG_RESULT + i * sizeof(uint32_t));
        return Error::None;
    }

    // Issues a command once the controller is idle and waits for the event
    // that reports its completion.
    static Error run(uint32_t command, uint32_t done) {
        const uintptr_t trng = base();
        const uint64_t idle_end = now_us() + IDLE_TIMEOUT_US;
        while ((mmio::read32(trng, TRNG_STATUS) & (TRNG_STATUS_GENERATING | TRNG_STATUS_SEEDING)) != 0) {
            ENSURE(now_us() < idle_end, Error::Timeout);
            arch_spin_hint();
        }
        mmio::write32(trng, TRNG_EVENTS, done);
        mmio::write32(trng, TRNG_COMMAND, command);
        const uint64_t end = now_us() + COMMAND_TIMEOUT_US;
        while ((mmio::read32(trng, TRNG_EVENTS) & done) == 0) {
            ENSURE(now_us() < end, Error::Timeout);
            arch_spin_hint();
        }
        mmio::write32(trng, TRNG_EVENTS, done);
        return Error::None;
    }

    static void enable_clocks() {
        const uintptr_t sys = BOARD_SYSCRG_PHYS + KERNEL_BASE;
        const uintptr_t stg = BOARD_STGCRG_PHYS + KERNEL_BASE;
        const uintptr_t bus = static_cast<uintptr_t>(BOARD_STG_BUS_CLOCK) * sizeof(uint32_t);
        mmio::write32(sys, bus, mmio::read32(sys, bus) | CLOCK_GATE_ENABLE);
        constexpr uint32_t clocks[] = {BOARD_TRNG_HCLK_CLOCK, BOARD_TRNG_AHB_CLOCK};
        for (uint32_t clock : clocks) {
            const uintptr_t offset = static_cast<uintptr_t>(clock) * sizeof(uint32_t);
            mmio::write32(stg, offset, mmio::read32(stg, offset) | CLOCK_GATE_ENABLE);
        }
        mmio::write32(stg, STG_RESET_ASSERT, mmio::read32(stg, STG_RESET_ASSERT) & ~(1U << BOARD_TRNG_RESET));
        arch_mb();
    }

    inline static Mutex mutex_{};
};

}  // namespace

namespace entropy {

Error start_platform() {
    if constexpr (BOARD_JH7110_TRNG)
        return Jh7110Trng::start();
    else
        return Error::NotSupported;
}

Result<size_t> read_platform(void* output, size_t size) {
    if constexpr (BOARD_JH7110_TRNG)
        return Jh7110Trng::read(output, size);
    else
        return Error::NoDevice;
}

}  // namespace entropy
