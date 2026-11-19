// File data: reading, writing, growing and truncating the cluster chain of a
// regular file, and updating its directory entry in place.

#include "fs/fat.h"

#include <sys/algorithm.hpp>
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/kernel_buffer.h"
#include <base/bpb.h>

Result<int> FatInfo::read_file(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size) {
    ENSURE(entry && (io_buf || size == 0) && size <= 0x7FFFFFFFU);
    ENSURE(!entry->is_directory());
    if (offset >= entry->file_size || size == 0) {
        return 0;
    }

    uint32_t max_size = entry->file_size - offset;
    if (size > max_size) {
        size = max_size;
    }

    uint32_t cluster = entry->cluster();
    ENSURE(cluster >= 2 && cluster < cluster_count_ + 2);

    auto scratch = TRY(KernelBuffer::alloc(bytes_per_cluster_));
    uint8_t* cluster_buf = scratch.data();
    uint32_t solve_bytes{};

    ClusterChain chain(*this, cluster);
    while (solve_bytes < size && (cluster = TRY(chain.next())) != 0) {
        uint32_t sector = cluster_to_sector(cluster);
        TRY(dev_->read(partition_start_lba_ + sector, cluster_buf, sectors_per_cluster_));

        if (offset >= bytes_per_cluster_) {
            offset -= bytes_per_cluster_;
            continue;
        }

        uint32_t cluster_offset = offset;
        uint32_t cluster_bytes = bytes_per_cluster_ - offset;
        offset = 0;

        uint32_t count = sys::min(cluster_bytes, size - solve_bytes);
        memcpy(io_buf + solve_bytes, cluster_buf + cluster_offset, count);

        solve_bytes += count;
    }

    ENSURE(solve_bytes == size, Error::BadFs);
    return static_cast<int>(solve_bytes);
}

Error FatInfo::read_file_entry(const EntryLocation& location, FatDirEntry* entry) {
    ENSURE(entry && location.index < SectorArray<FatDirEntry>::COUNT, Error::Invalid);
    ENSURE(location.sector >= data_start_sector_ && location.sector < total_sector_count_, Error::Invalid);
    auto storage = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    auto* sector = reinterpret_cast<SectorArray<FatDirEntry>*>(storage.data());
    TRY(dev_->read(partition_start_lba_ + location.sector, sector, 1));
    *entry = sector->entries[location.index];
    ENSURE(entry->is_valid(), Error::NotFound);
    ENSURE(!entry->is_directory(), Error::Invalid);
    return Error::None;
}

Error FatInfo::write_file_entry(const EntryLocation& location, const FatDirEntry& entry) {
    auto storage = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    auto* sector = reinterpret_cast<SectorArray<FatDirEntry>*>(storage.data());
    TRY(dev_->read(partition_start_lba_ + location.sector, sector, 1));
    sector->entries[location.index] = entry;
    return write_sectors(partition_start_lba_ + location.sector, sector, 1);
}

Error FatInfo::validate_file_chain(const FatDirEntry& entry) {
    if (entry.cluster() == 0) {
        ENSURE(entry.file_size == 0, Error::BadFs);
        return Error::None;
    }
    ClusterChain chain(*this, entry.cluster());
    uint32_t count{};
    while (TRY(chain.next()) != 0) {
        ++count;
    }
    ENSURE(static_cast<uint64_t>(count) * bytes_per_cluster_ >= entry.file_size, Error::BadFs);
    return Error::None;
}

Error FatInfo::link_file_cluster(uint32_t tail, uint32_t old_link, uint32_t cluster) {
    if (tail != 0) {
        Error error = write_entry(tail, cluster);
        if (error != Error::None) {
            // Never free a target until every FAT copy no longer references it.
            Error cleanup = write_entry(tail, old_link);
            if (cleanup == Error::None) {
                rollback_new_cluster(cluster);
            } else {
                klog::error("fat: retaining cluster %u after failed tail restore: %s\n", cluster, error_str(cleanup));
            }
            return error;
        }
    }
    return Error::None;
}

Result<int> FatInfo::write_file(const EntryLocation& location, const uint8_t* buf, uint32_t offset, uint32_t size,
                                bool append, size_t* end_offset) {
    ENSURE((buf || size == 0) && size <= 0x7FFFFFFFU, Error::Invalid);
    TRY(free_detached_chain());
    FatDirEntry entry{};
    TRY(read_file_entry(location, &entry));
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
    if (append) {
        offset = entry.file_size;
    }
    ENSURE(size <= 0xFFFFFFFFU - offset, Error::Invalid);
    if (size == 0) {
        if (end_offset) {
            *end_offset = offset;
        }
        return 0;
    }
    TRY(validate_file_chain(entry));

    sys::unique_ptr<WriteState> state(new (sys::nothrow) WriteState());
    ENSURE(state, Error::NoMem);
    state->location = location;
    state->entry = entry;
    state->bytes = buf;
    state->offset = offset;
    state->end = offset + size;
    // Keep confirmed progress when a later sector, allocation or metadata write fails.
    Error error = transfer_file(*state);
    if (error != Error::None && state->done == 0) {
        return error;
    }
    if (end_offset) {
        *end_offset = static_cast<size_t>(offset) + state->done;
    }
    return static_cast<int>(state->done);
}

Error FatInfo::transfer_file(WriteState& state) {
    state.position = sys::min(state.offset, state.entry.file_size);
    state.cluster = state.entry.cluster();
    uint32_t skip = state.position / bytes_per_cluster_;
    while (skip != 0) {
        ENSURE(valid_cluster(state.cluster), Error::BadFs);
        state.tail = state.cluster;
        state.cluster = TRY(read_entry(state.cluster));
        --skip;
    }
    state.cluster_offset = state.position % bytes_per_cluster_;
    while (state.position < state.end) {
        if (!valid_cluster(state.cluster)) {
            uint32_t old_link = fat::FAT32_EOC_MAX;
            if (state.tail != 0) {
                old_link = TRY(read_entry(state.tail));
                ENSURE(old_link >= fat::FAT32_EOC_MIN, Error::BadFs);
            }
            state.cluster = TRY(alloc_cluster());
            TRY(link_file_cluster(state.tail, old_link, state.cluster));
            if (state.tail == 0) {
                state.entry.set_cluster(state.cluster);
                // A failed directory write may already have published this cluster.
                TRY(write_file_entry(state.location, state.entry));
            }
        }
        uint32_t sector_offset = state.cluster_offset % bytes_per_sector_;
        uint32_t count = sys::min(bytes_per_sector_ - sector_offset, state.end - state.position);
        const bool gap = state.position < state.offset;
        if (gap) {
            count = sys::min(count, state.offset - state.position);
        }
        uint32_t lba =
            partition_start_lba_ + cluster_to_sector(state.cluster) + state.cluster_offset / bytes_per_sector_;
        if (sector_offset != 0 || count != bytes_per_sector_) {
            TRY(dev_->read(lba, state.sector.data(), 1));
        }
        if (gap) {
            memset(state.sector.data() + sector_offset, 0, count);
        } else {
            memcpy(state.sector.data() + sector_offset, state.bytes + state.done, count);
        }
        TRY(write_sectors(lba, state.sector.data(), 1));
        FatDirEntry updated = state.entry;
        updated.file_size = sys::max(state.entry.file_size, state.position + count);
        updated.attr |= FAT_ATTR_ARCHIVE;
        if (updated.file_size != state.entry.file_size || updated.attr != state.entry.attr) {
            TRY(write_file_entry(state.location, updated));
            state.entry = updated;
        }
        state.position += count;
        state.cluster_offset += count;
        if (!gap) {
            state.done += count;
        }
        if (state.position < state.end && state.cluster_offset == bytes_per_cluster_) {
            state.tail = state.cluster;
            state.cluster = TRY(read_entry(state.cluster));
            state.cluster_offset = 0;
        }
    }
    return Error::None;
}

Error FatInfo::truncate_file(const EntryLocation& location) {
    TRY(free_detached_chain());
    FatDirEntry entry{};
    TRY(read_file_entry(location, &entry));
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
    TRY(validate_file_chain(entry));
    const uint32_t cluster = entry.cluster();
    entry.set_cluster(0);
    entry.file_size = 0;
    entry.attr |= FAT_ATTR_ARCHIVE;
    // Detach first. On error retain the chain, since the write may be partial.
    TRY(write_file_entry(location, entry));
    pending_free_cluster_ = cluster;
    return free_detached_chain();
}
