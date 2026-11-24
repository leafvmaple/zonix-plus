#include "block/partition.h"
#include <kernel/log.h>

#include <base/gpt.h>
#include <base/mbr.h>

namespace {

constexpr uint32_t GPT_HEADER_LBA = 1;
constexpr uint32_t MAX_GPT_ENTRIES = 128;

}  // namespace

namespace blk {

Result<size_t> find_data_partitions(BlockDevice* disk, PartitionRange* out, size_t capacity) {
    ENSURE(disk && (out || capacity == 0), Error::Invalid);
    MbrHeader mbr{};
    TRY(disk->read(0, &mbr, 1));
    // A bare file system also ends its first sector with 0xAA55; only a
    // protective MBR followed by a GPT header counts as a partition table.
    if (!mbr.is_valid() || !mbr.partitions[0].is_gpt())
        return static_cast<size_t>(0);
    GptHeader header{};
    TRY(disk->read(GPT_HEADER_LBA, &header, 1));
    if (!header.is_valid() || header.partition_entry_size != sizeof(GptPartitionEntry))
        return static_cast<size_t>(0);

    using Sector = SectorArray<GptPartitionEntry>;
    Sector sector{};
    uint32_t entries = header.num_partition_entries < MAX_GPT_ENTRIES ? header.num_partition_entries : MAX_GPT_ENTRIES;
    size_t found = 0;
    for (uint32_t index = 0; index < entries && found < capacity; ++index) {
        if (index % Sector::COUNT == 0) {
            uint64_t lba = header.partition_entry_lba + index / Sector::COUNT;
            ENSURE(lba < disk->block_count, Error::BadFs);
            TRY(disk->read(static_cast<uint32_t>(lba), &sector, 1));
        }
        const auto& entry = sector.entries[index % Sector::COUNT];
        if (entry.is_empty() || !entry.is_basic_data())
            continue;
        if (entry.starting_lba == 0 || entry.ending_lba < entry.starting_lba || entry.ending_lba >= disk->block_count) {
            klog::warn("%s: partition %u lies outside the disk\n", disk->name, index + 1);
            continue;
        }
        out[found++] = {static_cast<int>(index + 1), static_cast<uint32_t>(entry.starting_lba),
                        static_cast<uint32_t>(entry.ending_lba - entry.starting_lba + 1)};
    }
    return found;
}

int scan_partitions() {
    return PartitionTable::scan();
}

}  // namespace blk

Error PartitionDevice::attach(BlockDevice* disk, const blk::PartitionRange& range) {
    ENSURE(disk && !disk_ && range.number > 0 && range.number < 100, Error::Invalid);
    ENSURE(range.first_lba <= disk->block_count && range.block_count <= disk->block_count - range.first_lba,
           Error::Invalid);
    size_t length = 0;
    for (; disk->name[length] && length < sizeof(name) - 4; ++length)
        name[length] = disk->name[length];
    name[length++] = 'p';
    if (range.number >= 10)
        name[length++] = static_cast<char>('0' + range.number / 10);
    name[length++] = static_cast<char>('0' + range.number % 10);
    name[length] = '\0';
    type = disk->type;
    block_count = range.block_count;
    first_lba_ = range.first_lba;
    disk_ = disk;
    return Error::None;
}

bool PartitionDevice::contains(uint32_t start_lba, size_t count) const {
    return disk_ && start_lba <= block_count && count <= block_count - start_lba;
}

Error PartitionDevice::read_blocks(uint32_t start_lba, void* buf, size_t count) {
    ENSURE(contains(start_lba, count), disk_ ? Error::Invalid : Error::NoDevice);
    return disk_->read_blocks(first_lba_ + start_lba, buf, count);
}

Error PartitionDevice::write_blocks(uint32_t start_lba, const void* buf, size_t count) {
    ENSURE(contains(start_lba, count), disk_ ? Error::Invalid : Error::NoDevice);
    return disk_->write_blocks(first_lba_ + start_lba, buf, count);
}

Error PartitionDevice::flush_cache() {
    ENSURE(disk_, Error::NoDevice);
    return disk_->flush_cache();
}

int PartitionTable::scan() {
    int disks = BlockManager::device_count();
    for (int i = 0; i < disks && count_ < partitions_.size(); ++i) {
        BlockDevice* disk = BlockManager::find_device(i);
        if (!disk || disk->type != blk::DeviceType::Disk)
            continue;
        sys::array<blk::PartitionRange, MAX_PARTITIONS> ranges{};
        auto found = blk::find_data_partitions(disk, ranges.data(), partitions_.size() - count_);
        if (!found.ok()) {
            klog::warn("%s: cannot read partition table (%d)\n", disk->name, static_cast<int>(found.error()));
            continue;
        }
        for (size_t p = 0; p < found.value(); ++p) {
            auto& partition = partitions_[count_];
            if (partition.attach(disk, ranges[p]) != Error::None || blk::register_device(&partition) != Error::None)
                return -1;
            ++count_;
            klog::info("partition: %s, %u MiB\n", partition.name, partition.block_count / 2048);
        }
    }
    return 0;
}
