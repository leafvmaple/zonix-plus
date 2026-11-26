#include "drivers/watchdog.h"
#include "block/blk.h"
#include "drivers/intr.h"
#include "fs/vfs.h"
#include "lib/kernel_buffer.h"
#include "lib/waitqueue.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <kernel/log.h>

namespace watchdog {
namespace {

// The machine resets about TIMEOUT_S after the last feed. Feeds come every
// FEED_MS while the root disk answered its last probe, made every PROBE_MS.
constexpr uint32_t TIMEOUT_S = 30;
constexpr uint64_t FEED_MS = 1000;
constexpr uint64_t PROBE_MS = 5000;

class Feeder {
public:
    // Arms the watchdog and starts the thread that feeds it. Machines
    // without one start no thread.
    static int start() {
        Error error = start_platform(TIMEOUT_S);
        if (error == Error::NotSupported)
            return 0;
        if (error != Error::None) {
            klog::warn("watchdog: not armed (%s)\n", error_str(error));
            return 0;
        }
        armed_ = true;
        auto pid = sched::kernel_thread(run, nullptr);
        if (!pid.ok()) {
            stop();
            return static_cast<int>(pid.error());
        }
        klog::info("watchdog: armed, resets %u s after the system stops\n", TIMEOUT_S);
        return 0;
    }

    static void tick() {
        if (!sleepers_.empty() && trap::uptime_ms() >= wake_ms_)
            sleepers_.wakeup_all();
    }

    static void stop() {
        intr::Guard guard;
        if (!armed_)
            return;
        armed_ = false;
        stop_platform();
    }

private:
    static int run(void*) {
        // Nothing the user runs may keep the feeder from its turn.
        sched::current()->priority = sched_prio::HIGHEST_PRIORITY;
        auto sector = KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES);
        bool healthy = true;
        uint64_t probe_ms = 0;
        for (;;) {
            uint64_t now = trap::uptime_ms();
            if (now >= probe_ms) {
                bool was_healthy = healthy;
                healthy = root_disk_answers(sector);
                if (was_healthy && !healthy)
                    klog::error("watchdog: the root disk failed; letting the watchdog reset the machine\n");
                probe_ms = now + PROBE_MS;
            }
            {
                intr::Guard guard;
                if (armed_ && healthy)
                    feed_platform();
            }
            wake_ms_ = now + FEED_MS;
            while (trap::uptime_ms() < wake_ms_)
                sleepers_.sleep();
        }
    }

    // A disk that has gone offline or fails a read leaves no way to run a
    // program, not even reboot. One frozen for shutdown still counts as healthy.
    static bool root_disk_answers(Result<KernelBuffer>& sector) {
        const char* name = vfs::mounted_device("/");
        BlockDevice* disk = name ? BlockManager::find_device(name) : nullptr;
        if (!disk || !sector.ok())
            return true;
        Error error = disk->read(0, sector.value().data(), 1);
        return error != Error::NoDevice && error != Error::Io;
    }

    inline static WaitQueue sleepers_{};
    inline static uint64_t wake_ms_{};
    inline static bool armed_{};
};

}  // namespace

int init() {
    return Feeder::start();
}

void tick() {
    Feeder::tick();
}

void stop() {
    Feeder::stop();
}

}  // namespace watchdog
