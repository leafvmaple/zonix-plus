#include "block/blk.h"

Error BlockManager::begin_io(ActivityGuard& guard) {
    if (flush_access_ && io_gate_.is_owner()) {
        return Error::None;
    }
    return guard.acquire(io_gate_);
}

Error BlockDevice::read(uint32_t start_lba, void* buf, size_t block_count) {
    ActivityGuard guard;
    TRY(BlockManager::begin_io(guard));
    return read_blocks(start_lba, buf, block_count);
}

Error BlockDevice::write(uint32_t start_lba, const void* buf, size_t block_count) {
    ActivityGuard guard;
    TRY(BlockManager::begin_io(guard));
    return write_blocks(start_lba, buf, block_count);
}

Error BlockDevice::flush() {
    ActivityGuard guard;
    TRY(BlockManager::begin_io(guard));
    return flush_cache();
}
