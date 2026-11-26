#include <kernel/power.h>
#include <asm/power.h>
#include "fs/vfs.h"
#include "block/blk.h"
#include "drivers/watchdog.h"
#include <kernel/log.h>

namespace power {

ShutdownGuard::ShutdownGuard(ShutdownGuard&& other) noexcept
    : files_frozen_(sys::exchange(other.files_frozen_, false)),
      blocks_frozen_(sys::exchange(other.blocks_frozen_, false)) {}

ShutdownGuard::~ShutdownGuard() {
    if (blocks_frozen_) {
        BlockManager::resume();
    }
    if (files_frozen_) {
        vfs::thaw();
    }
}

Result<ShutdownGuard> prepare_shutdown() {
    ShutdownGuard guard;
    TRY(vfs::freeze());
    guard.files_frozen_ = true;
    TRY(BlockManager::quiesce());
    guard.blocks_frozen_ = true;
    Error files = vfs::sync();
    Error devices = BlockManager::flush_all();
    // NoDevice means storage that has already gone offline. What it held is
    // lost whether or not the machine restarts, and refusing would leave a
    // machine whose root disk failed with no way to restart but power. Any
    // other failure means data could still be saved, so it stops shutdown.
    const Error results[]{files, devices};
    for (Error error : results) {
        if (error != Error::None && error != Error::NoDevice) {
            return error;
        }
    }
    if (files == Error::NoDevice || devices == Error::NoDevice) {
        klog::warn("power: storage that is offline was not synchronized\n");
    }
    BlockManager::finish_sync();
    return guard;
}

Error shutdown() {
    auto guard = TRY(prepare_shutdown());
    // A machine that is off must stay off rather than be reset back on.
    watchdog::stop();
    return arch_power_off();
}

Error reboot() {
    auto guard = TRY(prepare_shutdown());
    return arch_reboot();
}

}  // namespace power
