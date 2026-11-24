#include "fs/fat.h"
#include <kernel/log.h>

#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/kernel_buffer.h"

#include <sys/algorithm.hpp>
#include <base/bpb.h>
#include <base/gpt.h>
#include <base/mbr.h>

namespace {

constexpr uint32_t FAT_INVALID_SECTOR = static_cast<uint32_t>(-1);
constexpr uint32_t UNKNOWN_FREE_CLUSTERS = static_cast<uint32_t>(-1);
constexpr uint8_t ZERO_SECTOR[BlockDevice::BLOCK_SIZE_BYTES]{};

struct Fat32FsInfo {
    static constexpr uint32_t LEAD_SIGNATURE = 0x41615252;
    static constexpr uint32_t STRUCT_SIGNATURE = 0x61417272;
    static constexpr uint32_t TRAIL_SIGNATURE = 0xAA550000;

    uint32_t lead_signature;
    uint8_t reserved1[480];
    uint32_t struct_signature;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t reserved2[12];
    uint32_t trail_signature;

    bool is_valid() const {
        return lead_signature == LEAD_SIGNATURE && struct_signature == STRUCT_SIGNATURE &&
               trail_signature == TRAIL_SIGNATURE;
    }
};
static_assert(sizeof(Fat32FsInfo) == BlockDevice::BLOCK_SIZE_BYTES);

Result<uint32_t> find_partition_start(BlockDevice* dev) {
    MbrHeader mbr{};
    TRY_LOG(dev->read(0, &mbr, 1), "fat_mount: failed to read sector 0");

    ENSURE_LOG(mbr.is_valid(), Error::BadFs, "fat_mount: invalid MBR boot signature: 0x%04x", mbr.signature);

    if (mbr.partitions[0].is_gpt()) {
        GptHeader gpt{};
        TRY_LOG(dev->read(1, &gpt, 1), "fat_mount: failed to read GPT header");

        ENSURE_LOG(gpt.is_valid(), Error::BadFs, "fat_mount: bad GPT signature");

        int32_t esp_lba = gpt.find_esp_lba(
            [dev](uint32_t lba, void* sector_buf) { return static_cast<int>(dev->read(lba, sector_buf, 1)); });
        if (esp_lba < 0) {
            // Its data partitions may still hold a file system of their own.
            klog::debug("fat_mount: no EFI system partition\n");
            return Error::NotFound;
        }

        klog::debug("fat_mount: found ESP at LBA %d\n", static_cast<uint32_t>(esp_lba));
        return static_cast<uint32_t>(esp_lba);
    }

    // Traditional MBR.
    if (mbr.partitions[0].is_fat32()) {
        klog::debug("fat_mount: MBR detected, partition starts at LBA %d\n", mbr.partitions[0].start_lba);
        return mbr.partitions[0].start_lba;
    }

    // No partition table: assume BPB is at LBA 0.
    return 0U;
}

}  // namespace

void FatInfo::do_init_state(BlockDevice* dev, uint32_t partition_start, const Fat32BootSector& bs) {
    dev_ = dev;
    partition_start_lba_ = partition_start;
    total_sector_count_ = bs.total_sectors_32;
    bytes_per_sector_ = bs.bytes_per_sector;
    sectors_per_cluster_ = bs.sectors_per_cluster;
    bytes_per_cluster_ = bytes_per_sector_ * sectors_per_cluster_;

    reserved_sector_count_ = bs.reserved_sectors;
    fat_count_ = bs.num_fats;
    fat_sector_count_ = bs.fat_size_32;
    root_cluster_ = bs.root_cluster;

    fat_start_sector_ = reserved_sector_count_;
    data_start_sector_ = fat_start_sector_ + (fat_count_ * fat_sector_count_);

    uint32_t data_sectors = total_sector_count_ - data_start_sector_;
    cluster_count_ = data_sectors / sectors_per_cluster_;
    fat_type_ = fat::TYPE_FAT32;

    fs_info_sector_ = bs.fs_info < reserved_sector_count_ && bs.fs_info != bs.backup_boot_sector ? bs.fs_info : 0;
    free_clusters_ = UNKNOWN_FREE_CLUSTERS;
    fs_info_dirty_ = fs_info_sector_ != 0;
    buffer_sector_ = FAT_INVALID_SECTOR;
}

Error FatInfo::mount(BlockDevice* dev) {
    ENSURE(dev);

    auto part_start = TRY(find_partition_start(dev));

    Fat32BootSector bs{};
    TRY_LOG(dev->read(part_start, &bs, 1), "fat_mount: failed to read boot sector at LBA %d", part_start);

    ENSURE_LOG(bs.is_fat32(), Error::BadFs, "fat_mount: invalid boot signature: 0x%04x", bs.boot_signature_word);

    // The block layer and directory buffers support 512-byte logical sectors.
    ENSURE(bs.bytes_per_sector != 0, Error::BadFs);
    ENSURE(bs.bytes_per_sector == 512 || bs.bytes_per_sector == 1024 || bs.bytes_per_sector == 2048 ||
               bs.bytes_per_sector == 4096,
           Error::BadFs);
    ENSURE(bs.bytes_per_sector == BlockDevice::BLOCK_SIZE_BYTES, Error::NotSupported);
    ENSURE(bs.sectors_per_cluster != 0 && bs.sectors_per_cluster <= 128 &&
               (bs.sectors_per_cluster & (bs.sectors_per_cluster - 1)) == 0,
           Error::BadFs);
    ENSURE(bs.reserved_sectors != 0 && bs.num_fats != 0 && bs.num_fats <= 2 && bs.fat_size_32 != 0 &&
               bs.root_entries == 0 && bs.fat_size_16 == 0,
           Error::BadFs);
    ENSURE(bs.fs_version == 0 && (bs.ext_flags & 0x80) == 0, Error::NotSupported);
    const uint64_t data_start = bs.reserved_sectors + static_cast<uint64_t>(bs.num_fats) * bs.fat_size_32;
    ENSURE(data_start < bs.total_sectors_32, Error::BadFs);
    ENSURE(static_cast<uint64_t>(part_start) + bs.total_sectors_32 <= dev->block_count, Error::BadFs);
    const uint64_t clusters = (bs.total_sectors_32 - data_start) / bs.sectors_per_cluster;
    ENSURE(clusters != 0 && clusters + 2 <= fat::FAT32_RESERVED_MIN, Error::BadFs);
    ENSURE((clusters + 2) * sizeof(uint32_t) <= static_cast<uint64_t>(bs.fat_size_32) * bs.bytes_per_sector,
           Error::BadFs);
    ENSURE(bs.root_cluster >= 2 && bs.root_cluster < clusters + 2, Error::BadFs);

    do_init_state(dev, part_start, bs);

    char oem[9]{};
    char label[12]{};

    memcpy(oem, bs.oem, 8);
    memcpy(label, bs.volume_label, 11);

    klog::debug("fat: FAT%d on %s, label=%s OEM=%s partition LBA=%u\n", fat_type_, dev->name, label, oem, part_start);

    return Error::None;
}

Error FatInfo::unmount() {
    TRY(sync());
    dev_ = nullptr;
    buffer_sector_ = FAT_INVALID_SECTOR;
    return Error::None;
}

Error FatInfo::write_sectors(uint32_t start_lba, const void* buf, size_t block_count) {
    needs_sync_ = true;
    return dev_->write(start_lba, buf, block_count);
}

Error FatInfo::sync() {
    ENSURE(dev_, Error::NoDevice);
    needs_sync_ = true;
    TRY(free_detached_chain());
    TRY(flush_fat_buffer());
    TRY(sync_fs_info());
    TRY(dev_->flush());
    needs_sync_ = false;
    return Error::None;
}

Result<uint32_t> FatInfo::count_free_clusters() {
    constexpr uint32_t sectors_per_read = 8;
    constexpr uint32_t entries_per_sector = BlockDevice::BLOCK_SIZE_BYTES / sizeof(uint32_t);
    auto scratch = TRY(KernelBuffer::alloc(sectors_per_read * BlockDevice::BLOCK_SIZE_BYTES));
    const auto* entries = reinterpret_cast<const uint32_t*>(scratch.data());
    uint32_t count = 0;
    uint32_t end = cluster_count_ + 2;
    uint32_t end_sector = (end + entries_per_sector - 1) / entries_per_sector;
    for (uint32_t sector = 0; sector < end_sector;) {
        uint32_t sectors = sys::min(sectors_per_read, end_sector - sector);
        TRY(dev_->read(partition_start_lba_ + fat_start_sector_ + sector, scratch.data(), sectors));
        uint32_t first = sector * entries_per_sector;
        uint32_t limit = sys::min(sectors * entries_per_sector, end - first);
        for (uint32_t i = sector == 0 ? 2 : 0; i < limit; ++i) {
            if ((entries[i] & fat::FAT32_CLUSTER_MASK) == fat::FAT32_FREE)
                ++count;
        }
        sector += sectors;
    }
    return count;
}

Error FatInfo::sync_fs_info() {
    if (!fs_info_dirty_)
        return Error::None;

    auto scratch = TRY(KernelBuffer::alloc(sizeof(Fat32FsInfo)));
    auto& info = *reinterpret_cast<Fat32FsInfo*>(scratch.data());
    uint32_t lba = partition_start_lba_ + fs_info_sector_;
    TRY(dev_->read(lba, &info, 1));
    if (info.is_valid()) {
        // A previously mounted volume can already contain a stale count.
        if (free_clusters_ == UNKNOWN_FREE_CLUSTERS)
            free_clusters_ = TRY(count_free_clusters());
        if (info.free_count != free_clusters_) {
            info.free_count = free_clusters_;
            TRY(write_sectors(lba, &info, 1));
        }
    }
    fs_info_dirty_ = false;
    return Error::None;
}

void FatInfo::print() const {
    cprintf("FAT%d File System Information:\n", fat_type_);
    cprintf("  Bytes/Sector: %d\n", bytes_per_sector_);
    cprintf("  Sectors/Cluster: %d\n", sectors_per_cluster_);
    cprintf("  Bytes/Cluster: %d\n", bytes_per_cluster_);
    cprintf("  Total Sectors: %d\n", total_sector_count_);
    cprintf("  FAT Start: sector %d\n", fat_start_sector_);
    cprintf("  FAT Size: %d sectors\n", fat_sector_count_);
    cprintf("  Root Cluster: %d\n", root_cluster_);
    cprintf("  Root Start: sector %d\n", root_start_sector_);
    cprintf("  Root Entries: %d\n", root_entry_count_);
    cprintf("  Data Start: sector %d\n", data_start_sector_);
    cprintf("  Cluster Count: %d\n", cluster_count_);
}

Error FatInfo::flush_fat_buffer() {
    if (buffer_dirty_) {
        for (uint32_t i = 0; i < fat_count_; i++) {
            TRY(write_sectors(partition_start_lba_ + buffer_sector_ + i * fat_sector_count_, buffer_, 1));
        }
        buffer_dirty_ = false;
    }
    return Error::None;
}

Error FatInfo::load_fat_sector(uint32_t sector) {
    // Failed mirrored writes stay dirty, even when the next operation is a read.
    TRY(flush_fat_buffer());
    if (sector != buffer_sector_) {
        auto buffer = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
        TRY(dev_->read(partition_start_lba_ + sector, buffer.data(), 1));
        memcpy(buffer_, buffer.data(), sizeof(buffer_));
        buffer_sector_ = sector;
    }
    return Error::None;
}

Result<uint32_t> FatInfo::read_entry(uint32_t cluster) {
    ENSURE(dev_ && valid_cluster(cluster), Error::BadFs);

    uint32_t fat_offset = cluster << 2;
    uint32_t fat_sector = fat_start_sector_ + (fat_offset / bytes_per_sector_);
    uint32_t ent_offset = fat_offset % bytes_per_sector_;
    TRY(load_fat_sector(fat_sector));

    uint32_t value = *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]);
    return value & fat::FAT32_CLUSTER_MASK;
}

Error FatInfo::write_entry(uint32_t cluster, uint32_t value) {
    ENSURE(cluster >= 2 && cluster < cluster_count_ + 2);

    uint32_t fat_offset = cluster << 2;
    uint32_t fat_sector = fat_start_sector_ + (fat_offset / bytes_per_sector_);
    uint32_t ent_offset = fat_offset % bytes_per_sector_;

    TRY(load_fat_sector(fat_sector));

    // Preserve top 4 reserved bits.
    uint32_t old = *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]);
    uint32_t updated = value & fat::FAT32_CLUSTER_MASK;
    *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]) = (old & ~fat::FAT32_CLUSTER_MASK) | updated;
    buffer_dirty_ = true;

    // Match the cached FAT transition once. Failed writes retain that sector for retry.
    bool was_free = (old & fat::FAT32_CLUSTER_MASK) == fat::FAT32_FREE;
    bool is_free = updated == fat::FAT32_FREE;
    if (free_clusters_ != UNKNOWN_FREE_CLUSTERS && was_free != is_free) {
        if (is_free)
            ++free_clusters_;
        else
            --free_clusters_;
        fs_info_dirty_ = true;
    }

    // Write through to all FAT copies immediately.
    return flush_fat_buffer();
}

Result<uint32_t> FatInfo::alloc_cluster() {
    for (uint32_t c = 2; c < cluster_count_ + 2; c++) {
        if (TRY(read_entry(c)) == fat::FAT32_FREE) {
            Error error = write_entry(c, fat::FAT32_EOC_MAX);
            if (error != Error::None) {
                rollback_new_cluster(c);
                return error;
            }

            uint32_t sector = cluster_to_sector(c);
            for (uint32_t s = 0; s < sectors_per_cluster_; s++) {
                error = write_sectors(partition_start_lba_ + sector + s, ZERO_SECTOR, 1);
                if (error != Error::None) {
                    rollback_new_cluster(c);
                    return error;
                }
            }
            return c;
        }
    }
    return Error::Full;
}

Error FatInfo::free_chain(uint32_t start_cluster) {
    // Validate before modifying FAT links; otherwise clearing links hides a cycle.
    ClusterChain chain(*this, start_cluster);
    while (TRY(chain.next()) != 0) {}
    uint32_t cluster = start_cluster;
    while (cluster < fat::FAT32_EOC_MIN) {
        uint32_t next = TRY(read_entry(cluster));
        TRY(write_entry(cluster, fat::FAT32_FREE));
        cluster = next;
    }
    return Error::None;
}

Error FatInfo::free_detached_chain() {
    while (pending_free_cluster_ != 0) {
        if (pending_free_next_ == 0) {
            pending_free_next_ = TRY(read_entry(pending_free_cluster_));
        }
        // Preserve the next link across a failed write, even if the cache now says FREE.
        TRY(write_entry(pending_free_cluster_, fat::FAT32_FREE));
        pending_free_cluster_ = pending_free_next_ >= fat::FAT32_EOC_MIN ? 0 : pending_free_next_;
        pending_free_next_ = 0;
    }
    return Error::None;
}

uint32_t FatInfo::cluster_to_sector(uint32_t cluster) const {
    if (cluster < 2) {
        return 0;
    }

    return data_start_sector_ + ((cluster - 2) * sectors_per_cluster_);
}

bool FatInfo::valid_cluster(uint32_t cluster) const {
    return cluster >= 2 && cluster < cluster_count_ + 2;
}

Result<uint32_t> FatInfo::ClusterChain::next() {
    if (finished_) {
        return 0U;
    }
    if (started_) {
        const uint32_t next = TRY(fat_.read_entry(current_));
        if (next >= fat::FAT32_EOC_MIN) {
            finished_ = true;
            return 0U;
        }
        ENSURE(fat_.valid_cluster(next), Error::BadFs);
        ++distance_;
        ENSURE(next != checkpoint_, Error::BadFs);
        if (distance_ == power_) {
            checkpoint_ = next;
            power_ *= 2;
            distance_ = 0;
        }
        current_ = next;
    }
    ENSURE(fat_.valid_cluster(current_) && visited_ < fat_.cluster_count_, Error::BadFs);
    ++visited_;
    started_ = true;
    return current_;
}
