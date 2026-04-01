#include "fs/fat.h"

#include "lib/memory.h"
#include "lib/stdio.h"

#include <base/bpb.h>
#include <base/gpt.h>
#include <base/mbr.h>

namespace {

constexpr uint32_t FAT_INVALID_SECTOR = static_cast<uint32_t>(-1);

int32_t find_partition_start(BlockDevice* dev) {
    MbrHeader mbr{};
    if (dev->read(0, &mbr, 1) != 0) {
        cprintf("fat_mount: failed to read sector 0\n");
        return -1;
    }

    if (mbr.signature != 0xAA55) {
        cprintf("fat_mount: invalid boot signature: 0x%04x\n", mbr.signature);
        return -1;
    }

    if (mbr.partitions[0].type == GPT_PROTECTIVE_MBR_TYPE) {
        uint8_t buf[512]{};
        if (dev->read(1, buf, 1) != 0) {
            cprintf("fat_mount: failed to read GPT header\n");
            return -1;
        }

        auto* gpt = reinterpret_cast<GptHeader*>(buf);
        if (!gpt->is_valid()) {
            cprintf("fat_mount: bad GPT signature\n");
            return -1;
        }

        int32_t esp_lba =
            gpt->find_esp_lba([dev](uint32_t lba, void* sector_buf) { return dev->read(lba, sector_buf, 1); });

        cprintf("fat_mount: found ESP at LBA %d\n", static_cast<uint32_t>(esp_lba));
        return esp_lba;
    }

    // Traditional MBR.
    if (mbr.partitions[0].type == PART_TYPE_FAT32_LBA || mbr.partitions[0].type == PART_TYPE_FAT32) {
        cprintf("fat_mount: MBR detected, partition starts at LBA %d\n", mbr.partitions[0].start_lba);
        return static_cast<int32_t>(mbr.partitions[0].start_lba);
    }

    // No partition table: assume BPB is at LBA 0.
    return 0;
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

int FatInfo::mount(BlockDevice* dev) {
    if (!dev) {
        return -1;
    }

    int32_t part_start = find_partition_start(dev);
    if (part_start < 0) {
        return -1;
    }

    uint32_t partition_start = static_cast<uint32_t>(part_start);

    Fat32BootSector bs{};
    if (dev->read(partition_start, &bs, 1) != 0) {
        cprintf("fat_mount: failed to read boot sector at LBA %d\n", partition_start);
        return -1;
    }

    if (bs.boot_signature_word != 0xAA55) {
        cprintf("fat_mount: invalid boot signature: 0x%04x\n", bs.boot_signature_word);
        return -1;
    }

    if (bs.total_sectors_32 == 0) {
        cprintf("fat_mount: invalid FAT32 total sectors\n");
        return -1;
    }

    do_init_state(dev, partition_start, bs);

    char oem[9]{};
    char label[12]{};

    memcpy(oem, bs.oem, 8);
    memcpy(label, bs.volume_label, 11);

    cprintf("FAT%d mounted: %s\n", fat_type_, label);
    cprintf("  OEM: %s\n", oem);
    cprintf("  Partition Start: LBA %d\n", partition_start);

    print();

    return 0;
}

void FatInfo::unmount() {
    if (buffer_dirty_ && buffer_sector_ != FAT_INVALID_SECTOR) {
        for (uint32_t i = 0; i < num_fats_; i++) {
            uint32_t fat_sector = fat_start_ + (i * fat_size_) + (buffer_sector_ - fat_start_);
            if (dev_->write(partition_start_ + fat_sector, buffer_, 1) != 0) {
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

uint32_t FatInfo::read_entry(uint32_t cluster) {
    if (cluster < 2 || cluster >= cluster_count_ + 2) {
        return 0;
    }

    if (fat_type_ != fat::TYPE_FAT32) {
        return 0;
    }

    uint32_t fat_offset = cluster << 2;
    uint32_t fat_sector = fat_start_ + (fat_offset / bytes_per_sector_);
    uint32_t ent_offset = fat_offset % bytes_per_sector_;

    if (fat_sector != buffer_sector_) {
        if (dev_->read(partition_start_ + fat_sector, buffer_, 1) != 0) {
            return 0;
        }
        buffer_sector_ = fat_sector;
    }

    uint32_t value = *reinterpret_cast<uint32_t*>(&buffer_[ent_offset]);
    return value & fat::FAT32_CLUSTER_MASK;
}

int FatInfo::write_entry(uint32_t cluster, uint32_t value) {
    // TODO: Implement FAT write support.
    static_cast<void>(cluster);
    static_cast<void>(value);
    return -1;
}

uint32_t FatInfo::cluster_to_sector(uint32_t cluster) const {
    if (cluster < 2) {
        return 0;
    }

    return data_start_ + ((cluster - 2) * sectors_per_cluster_);
}
