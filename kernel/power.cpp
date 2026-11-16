#include <kernel/power.h>
#include <asm/power.h>
#include "fs/vfs.h"
#include "block/blk.h"

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
    Error error = vfs::sync();
    Error devices = BlockManager::flush_all();
    if (error != Error::None) {
        return error;
    }
    TRY(devices);
    BlockManager::finish_sync();
    return guard;
}

Error shutdown() {
    auto guard = TRY(prepare_shutdown());
    return arch_power_off();
}

}  // namespace power
