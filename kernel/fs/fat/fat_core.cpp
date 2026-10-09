#include "fs/fat.h"

#include "lib/memory.h"
#include "lib/stdio.h"

#include <base/bpb.h>
#include <base/gpt.h>
#include <base/mbr.h>

namespace {

constexpr uint32_t FAT_INVALID_SECTOR = static_cast<uint32_t>(-1);

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
        ENSURE_LOG(esp_lba >= 0, Error::NotFound, "fat_mount: ESP partition not found");

        cprintf("fat_mount: found ESP at LBA %d\n", static_cast<uint32_t>(esp_lba));
        return static_cast<uint32_t>(esp_lba);
    }

    // Traditional MBR.
    if (mbr.partitions[0].is_fat32()) {
        cprintf("fat_mount: MBR detected, partition starts at LBA %d\n", mbr.partitions[0].start_lba);
        return mbr.partitions[0].start_lba;
    }

    // No partition table: assume BPB is at LBA 0.
    return 0U;
}

}  // namespace

void FatInfo::do_init_state(BlockDevice* dev, uint32_t partition_start, const Fat32BootSector& bs) {
    dev_ = dev;
    partition_start_ = partition_start;
    total_sectors_ = bs.total_sectors_32;
    bytes_per_sector_ = bs.bytes_per_sector;
    sectors_per_cluster_ = bs.sectors_per_cluster;
    bytes_per_cluster_ = bytes_per_sector_ * sectors_per_cluster_;

    reserved_sectors_ = bs.reserved_sectors;
    num_fats_ = bs.num_fats;
    fat_size_ = bs.fat_size_32;
    root_cluster_ = bs.root_cluster;

    fat_start_ = reserved_sectors_;
    data_start_ = fat_start_ + (num_fats_ * fat_size_);

    uint32_t data_sectors = total_sectors_ - data_start_;
    cluster_count_ = data_sectors / sectors_per_cluster_;
    fat_type_ = fat::TYPE_FAT32;

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
    ENSURE(bs.bytes_per_sector == BlockDevice::SIZE, Error::NotSupported);
    ENSURE(bs.sectors_per_cluster != 0 && bs.sectors_per_cluster <= 128 &&
               (bs.sectors_per_cluster & (bs.sectors_per_cluster - 1)) == 0,
           Error::BadFs);
    ENSURE(bs.reserved_sectors != 0 && bs.num_fats != 0 && bs.num_fats <= 2 && bs.fat_size_32 != 0 &&
               bs.root_entries == 0 && bs.fat_size_16 == 0,
           Error::BadFs);
    ENSURE(bs.fs_version == 0 && (bs.ext_flags & 0x80) == 0, Error::NotSupported);
    const uint64_t data_start = bs.reserved_sectors + static_cast<uint64_t>(bs.num_fats) * bs.fat_size_32;
    ENSURE(data_start < bs.total_sectors_32, Error::BadFs);
    ENSURE(static_cast<uint64_t>(part_start) + bs.total_sectors_32 <= dev->size, Error::BadFs);
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

    cprintf("FAT%d mounted: %s\n", fat_type_, label);
    cprintf("  OEM: %s\n", oem);
    cprintf("  Partition Start: LBA %d\n", part_start);

    return Error::None;
}

void FatInfo::unmount() {
    if (buffer_dirty_ && buffer_sector_ != FAT_INVALID_SECTOR) {
        for (uint32_t i = 0; i < num_fats_; i++) {
            uint32_t fat_sector = fat_start_ + (i * fat_size_) + (buffer_sector_ - fat_start_);
            if (dev_->write(partition_start_ + fat_sector, buffer_, 1) != Error::None) {
                cprintf("fat_unmount: failed to write FAT sector %d\n", fat_sector);
            }
        }
        buffer_dirty_ = false;
    }

    dev_ = nullptr;
    buffer_sector_ = FAT_INVALID_SECTOR;
    buffer_dirty_ = false;
}

void FatInfo::print() const {
    cprintf("FAT%d File System Information:\n", fat_type_);
    cprintf("  Bytes/Sector: %d\n", bytes_per_sector_);
    cprintf("  Sectors/Cluster: %d\n", sectors_per_cluster_);
    cprintf("  Bytes/Cluster: %d\n", bytes_per_cluster_);
    cprintf("  Total Sectors: %d\n", total_sectors_);
    cprintf("  FAT Start: sector %d\n", fat_start_);
    cprintf("  FAT Size: %d sectors\n", fat_size_);
    cprintf("  Root Cluster: %d\n", root_cluster_);
    cprintf("  Root Start: sector %d\n", root_start_);
    cprintf("  Root Entries: %d\n", root_entries_);
    cprintf("  Data Start: sector %d\n", data_start_);
    cprintf("  Cluster Count: %d\n", cluster_count_);
}

Result<uint32_t> FatInfo::read_entry(uint32_t cluster) {
    ENSURE(dev_ && valid_cluster(cluster), Error::BadFs);

    uint32_t fat_offset = cluster << 2;
    uint32_t fat_sector = fat_start_ + (fat_offset / bytes_per_sector_);
    uint32_t ent_offset = fat_offset % bytes_per_sector_;

    if (fat_sector != buffer_sector_) {
        TRY(dev_->read(partition_start_ + fat_sector, buffer_, 1));
        buffer_sector_ = fat_sector;
    }

    uint32_t value = *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]);
    return value & fat::FAT32_CLUSTER_MASK;
}

Error FatInfo::write_entry(uint32_t cluster, uint32_t value) {
    ENSURE(cluster >= 2 && cluster < cluster_count_ + 2);

    uint32_t fat_offset = cluster << 2;
    uint32_t fat_sector = fat_start_ + (fat_offset / bytes_per_sector_);
    uint32_t ent_offset = fat_offset % bytes_per_sector_;

    if (fat_sector != buffer_sector_) {
        if (buffer_dirty_ && buffer_sector_ != FAT_INVALID_SECTOR) {
            for (uint32_t i = 0; i < num_fats_; i++) {
                uint32_t abs_sector = partition_start_ + buffer_sector_ + (i * fat_size_);
                TRY(dev_->write(abs_sector, buffer_, 1));
            }
            buffer_dirty_ = false;
        }

        TRY(dev_->read(partition_start_ + fat_sector, buffer_, 1));
        buffer_sector_ = fat_sector;
    }

    // Preserve top 4 reserved bits.
    uint32_t old = *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]);
    *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]) = (old & 0xF0000000) | (value & fat::FAT32_CLUSTER_MASK);
    buffer_dirty_ = true;

    // Write through to all FAT copies immediately.
    for (uint32_t i = 0; i < num_fats_; i++) {
        uint32_t abs_sector = partition_start_ + fat_sector + (i * fat_size_);
        TRY(dev_->write(abs_sector, buffer_, 1));
    }
    buffer_dirty_ = false;

    return Error::None;
}

Result<uint32_t> FatInfo::alloc_cluster() {
    for (uint32_t c = 2; c < cluster_count_ + 2; c++) {
        if (TRY(read_entry(c)) == fat::FAT32_FREE) {
            TRY(write_entry(c, fat::FAT32_EOC_MAX));

            uint8_t zero[512]{};
            uint32_t sector = cluster_to_sector(c);
            for (uint32_t s = 0; s < sectors_per_cluster_; s++) {
                TRY(dev_->write(partition_start_ + sector + s, zero, 1));
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

uint32_t FatInfo::cluster_to_sector(uint32_t cluster) const {
    if (cluster < 2) {
        return 0;
    }

    return data_start_ + ((cluster - 2) * sectors_per_cluster_);
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
