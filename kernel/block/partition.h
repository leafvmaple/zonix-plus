#pragma once

// GPT data partitions as block devices of their own ("mmc0p5"), so a file
// system can live on one partition of a disk that holds others.
//
// Only basic-data partitions are published. An EFI system partition stays
// reachable through its whole disk, as the FAT driver already finds it there,
// and partitions of other systems are left alone.

#include <base/types.h>
#include "block/blk.h"
#include <sys/array.hpp>
#include "lib/result.h"

namespace blk {

struct PartitionRange {
    int number;          // 1-based GPT entry number
    uint32_t first_lba;  // Relative to the disk
    uint32_t block_count;
};

// Reads the GPT of `disk` and stores up to `capacity` basic-data partitions.
// Returns how many were found; a disk without a GPT has none.
Result<size_t> find_data_partitions(BlockDevice* disk, PartitionRange* out, size_t capacity);

// Publishes the data partitions of every registered disk.
int scan_partitions();

}  // namespace blk

class PartitionDevice final : public BlockDevice {
public:
    Error attach(BlockDevice* disk, const blk::PartitionRange& range);

private:
    Error read_blocks(uint32_t start_lba, void* buf, size_t block_count) override;
    Error write_blocks(uint32_t start_lba, const void* buf, size_t block_count) override;
    Error flush_cache() override;
    bool contains(uint32_t start_lba, size_t count) const;

    BlockDevice* disk_{};
    uint32_t first_lba_{};
};

class PartitionTable {
public:
    static constexpr size_t MAX_PARTITIONS = 4;

    static int scan();

private:
    inline static sys::array<PartitionDevice, MAX_PARTITIONS> partitions_{};
    inline static size_t count_{};
};
