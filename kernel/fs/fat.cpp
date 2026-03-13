#include "fat.h"
#include "lib/stdio.h"
#include "lib/math.h"
#include "lib/memory.h"
#include "lib/string.h"
#include <base/mbr.h>
#include <base/bpb.h>
#include <base/gpt.h>

// ---------------------------------------------------------------------------
// Helper: find partition start LBA from MBR or GPT
// Returns 0 if BPB is directly at sector 0 (no partition table, e.g. floppy)
// Returns start LBA > 0 if a FAT partition was found
// Returns -1 on error (can't read disk)
// ---------------------------------------------------------------------------
static int32_t find_partition_start(BlockDevice* dev) {
    mbr_t mbr{};
    if (dev->read(0, &mbr, 1) != 0) {
        cprintf("fat_mount: failed to read sector 0\n");
        return -1;
    }

    if (mbr.signature != 0xAA55) {
        cprintf("fat_mount: invalid boot signature: 0x%04x\n", mbr.signature);
        return -1;
    }

    // Check for GPT protective MBR
    if (mbr.partitions[0].type == GPT_PROTECTIVE_MBR_TYPE) {
        // Read GPT header at LBA 1
        uint8_t buf[512]{};
        if (dev->read(1, buf, 1) != 0) {
            cprintf("fat_mount: failed to read GPT header\n");
            return -1;
        }

        auto* gpt = reinterpret_cast<GptHeader*>(buf);
        if (gpt->signature != GPT_HEADER_SIGNATURE) {
            cprintf("fat_mount: bad GPT signature\n");
            return -1;
        }

        cprintf("fat_mount: GPT detected, scanning partition entries...\n");

        // Read partition entries to find the ESP
        uint64_t entry_lba = gpt->partition_entry_lba;
        uint32_t num_entries = gpt->num_partition_entries;
        uint32_t entry_size = gpt->partition_entry_size;
        uint32_t entries_per_sector = 512 / entry_size;

        for (uint32_t i = 0; i < num_entries; i++) {
            uint32_t sector = entry_lba + (i / entries_per_sector);
            uint32_t offset = (i % entries_per_sector) * entry_size;

            if (dev->read(sector, buf, 1) != 0) {
                break;
            }

            auto* part = reinterpret_cast<GptPartitionEntry*>(buf + offset);

            // Empty entry — stop scanning
            if (part->type_guid.data1 == 0 && part->type_guid.data2 == 0 && part->type_guid.data3 == 0)
                break;

            if (is_esp_guid(&part->type_guid)) {
                cprintf("fat_mount: found ESP at LBA %d\n", (uint32_t)part->starting_lba);
                return (int32_t)part->starting_lba;
            }
        }

        cprintf("fat_mount: no ESP found in GPT\n");
        return -1;
    }

    // Traditional MBR — check for FAT32 partition
    if (mbr.partitions[0].type == PART_TYPE_FAT32_LBA || mbr.partitions[0].type == PART_TYPE_FAT32) {
        cprintf("fat_mount: MBR detected, partition starts at LBA %d\n", mbr.partitions[0].start_lba);
        return (int32_t)mbr.partitions[0].start_lba;
    }

    // No MBR/GPT partition — assume BPB is at sector 0 (e.g. FAT floppy)
    return 0;
}

int FatInfo::mount(BlockDevice* dev) {
    if (!dev) {
        return -1;
    }

    int32_t part_start = find_partition_start(dev);
    if (part_start < 0) {
        return -1;
    }

    uint32_t partition_start = (uint32_t)part_start;

    struct fat32_boot_sector bs{};
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

    dev_ = dev;
    partition_start_ = partition_start;
    total_sectors_ = bs.total_sectors_32;
    bytes_per_sector_ = bs.bytes_per_sector;
    sectors_per_cluster_ = bs.sectors_per_cluster;
    bytes_per_cluster_ = bs.bytes_per_sector * bs.sectors_per_cluster;

    reserved_sectors_ = bs.reserved_sectors;
    num_fats_ = bs.num_fats;
    fat_size_ = bs.fat_size_32;
    root_entries_ = 0;
    root_cluster_ = bs.root_cluster;

    // Calculate FAT start sector (relative to partition)
    root_start_ = 0;
    root_sectors_ = 0;
    fat_start_ = reserved_sectors_;
    data_start_ = fat_start_ + (num_fats_ * fat_size_);

    // Calculate cluster count
    uint32_t data_sectors = total_sectors_ - data_start_;
    cluster_count_ = data_sectors / sectors_per_cluster_;
    fat_type_ = FAT_TYPE_FAT32;

    // Initialize FAT cache
    buffer_sector_ = (uint32_t)-1;  // Invalid sector
    buffer_dirty_ = false;

    // Print file system information
    char oem[9]{};
    char label[12]{};

    memcpy(oem, bs.oem, 8);
    memcpy(label, bs.volume_label, 11);

    cprintf("FAT%d mounted: %s\n", fat_type_, label);
    cprintf("  OEM: %s\n", oem);
    cprintf("  Partition Start: LBA %d\n", partition_start);
    print_info();

    return 0;
}

void FatInfo::unmount() {
    // Flush FAT buffer if dirty
    if (buffer_dirty_ && buffer_sector_ != (uint32_t)-1) {
        for (uint32_t i = 0; i < num_fats_; i++) {
            uint32_t fat_sector = fat_start_ + (i * fat_size_) + (buffer_sector_ - fat_start_);
            if (dev_->write(partition_start_ + fat_sector, buffer_, 1) != 0) {
                cprintf("fat_unmount: failed to write FAT sector %d\n", fat_sector);
            }
        }
        buffer_dirty_ = false;
    }

    // Clear info
    dev_ = nullptr;
    buffer_sector_ = (uint32_t)-1;
    buffer_dirty_ = 0;
}

void FatInfo::print_info() {
    cprintf("FAT%d File System Information:\n", fat_type_);
    cprintf("  Bytes/Sector: %d\n", bytes_per_sector_);
    cprintf("  Sectors/Cluster: %d\n", sectors_per_cluster_);
    cprintf("  Bytes/Cluster: %d\n", bytes_per_cluster_);
    cprintf("  Total Sectors: %d\n", total_sectors_);
    cprintf("  FAT Start: sector %d\n", fat_start_);
    cprintf("  FAT Size: %d sectors\n", fat_size_);
    if (fat_type_ == FAT_TYPE_FAT32) {
        cprintf("  Root Cluster: %d\n", root_cluster_);
    } else {
        cprintf("  Root Start: sector %d\n", root_start_);
        cprintf("  Root Entries: %d\n", root_entries_);
    }
    cprintf("  Data Start: sector %d\n", data_start_);
    cprintf("  Cluster Count: %d\n", cluster_count_);
}

uint32_t FatInfo::get_cluster(fat_dir_entry_t& entry) {
    uint32_t cluster = entry.first_cluster_low;
    cluster |= ((uint32_t)entry.first_cluster_high) << 16;

    return cluster;
}

uint32_t FatInfo::read_entry(uint32_t cluster) {
    if (cluster < 2 || cluster >= cluster_count_ + 2) {
        return 0;
    }

    if (fat_type_ != FAT_TYPE_FAT32) {
        return 0;
    }
    uint32_t fat_offset = cluster << 2;
    uint32_t fat_sector = fat_start_ + (fat_offset / bytes_per_sector_);
    uint32_t ent_offset = fat_offset % bytes_per_sector_;

    // Load FAT sector if not cached
    if (fat_sector != buffer_sector_) {
        if (dev_->read(partition_start_ + fat_sector, buffer_, 1) != 0) {
            return 0;
        }
        buffer_sector_ = fat_sector;
    }

    uint32_t value = *(uint32_t*)&buffer_[ent_offset];
    return value & FAT32_CLUSTER_MASK;  // Mask out top 4 bits
}

int FatInfo::write_entry(uint32_t cluster, uint32_t value) {
    // TODO: Implement FAT write support
    (void)cluster;
    (void)value;
    return -1;
}

uint32_t FatInfo::cluster_to_sector(uint32_t cluster) const {
    if (cluster < 2) {
        return 0;
    }

    return data_start_ + ((cluster - 2) * sectors_per_cluster_);
}

bool FatInfo::is_valid_entry(fat_dir_entry_t& entry) {
    if (entry.name[0] == 0x00 || entry.name[0] == (char)0xE5) {
        return false;
    }

    if ((entry.attr & FAT_ATTR_VOLUME_ID) || (entry.attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
        return false;
    }

    return true;
}

void FatInfo::get_filename(fat_dir_entry_t* entry, char* buf, int bufsize) {
    if (!entry || !buf || bufsize < 13) {
        return;
    }

    int out{};
    // Copy base name (trim trailing spaces)
    for (int i = 0; i < 8 && entry->name[i] != ' '; ++i) {
        buf[out++] = entry->name[i];
    }

    // Copy extension if present
    if (entry->ext[0] != ' ') {
        buf[out++] = '.';
        for (int i = 0; i < 3 && entry->ext[i] != ' '; ++i) {
            buf[out++] = entry->ext[i];
        }
    }

    buf[out] = '\0';
}

int FatInfo::read_root_dir(int (*callback)(fat_dir_entry_t* entry, void* arg), void* arg) {
    if (!callback) {
        return -1;
    }

    int count{};

    if (fat_type_ != FAT_TYPE_FAT32) {
        return -1;
    }

    uint8_t sector_buf[512];
    for (uint32_t cluster = root_cluster_; cluster >= 2 && cluster < FAT32_EOC_MIN; cluster = read_entry(cluster)) {
        uint32_t sector = cluster_to_sector(cluster);

        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            if (dev_->read(partition_start_ + sector + i, sector_buf, 1) != 0) {
                cprintf("fat_read_root_dir: failed to read sector %d\n", sector + i);
                return -1;
            }

            fat_dir_entry_t* entries = (fat_dir_entry_t*)sector_buf;
            for (uint32_t j = 0; j < bytes_per_sector_ / 32; j++) {
                fat_dir_entry_t& entry = entries[j];

                if (entry.name[0] == 0x00) {
                    return count;
                }

                if (!is_valid_entry(entry)) {
                    continue;
                }

                if (callback(&entry, arg) != 0) {
                    return count;
                }

                count++;
            }
        }
    }

    return count;
}

int FatInfo::find_file(const char* filename, fat_dir_entry_t* result) {
    if (!filename || !result) {
        return -1;
    }
    if (fat_type_ != FAT_TYPE_FAT32) {
        return -1;
    }

    // Convert filename to 8.3 format
    char name[8]{};
    char ext[3]{};
    memset(name, ' ', sizeof(name));
    memset(ext, ' ', sizeof(ext));

    const char* dot = strchr(filename, '.');
    if (dot) {
        memcpy(name, filename, min(dot - filename, (long)8));
        memcpy(ext, dot + 1, min(strlen(dot + 1), (size_t)3));
    } else {
        memcpy(name, filename, min(strlen(filename), (size_t)8));
    }

    for (char& c : name) {
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
    }
    for (char& c : ext) {
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
    }

    uint8_t sector_buf[512];
    for (uint32_t cluster = root_cluster_; cluster >= 2 && cluster < FAT32_EOC_MIN; cluster = read_entry(cluster)) {
        uint32_t sector = cluster_to_sector(cluster);

        for (uint32_t s = 0; s < sectors_per_cluster_; s++, sector++) {
            if (dev_->read(partition_start_ + sector, sector_buf, 1) != 0) {
                return -1;
            }

            fat_dir_entry_t* entries = (fat_dir_entry_t*)sector_buf;
            for (uint32_t j = 0; j < bytes_per_sector_ / 32; j++) {
                fat_dir_entry_t& entry = entries[j];

                if (entry.name[0] == 0x00) {
                    return -1;  // End of directory
                }

                if (!is_valid_entry(entry)) {
                    continue;
                }

                if (memcmp(entry.name, name, 8) == 0 && memcmp(entry.ext, ext, 3) == 0) {
                    memcpy(result, &entry, sizeof(fat_dir_entry_t));
                    return 0;
                }
            }
        }
    }

    return -1;  // Not found
}

int FatInfo::read_file(fat_dir_entry_t* entry, uint8_t* buf, uint32_t offset, uint32_t size) {
    if (!entry || !buf) {
        return -1;
    }

    // Check if it's a directory
    if (entry->attr & FAT_ATTR_DIRECTORY) {
        cprintf("fat_read_file: cannot read directory\n");
        return -1;
    }

    // Check offset and size
    if (offset >= entry->file_size) {
        return 0;  // No data to read
    }

    if (offset + size > entry->file_size) {
        size = entry->file_size - offset;
    }

    // Get starting cluster
    uint32_t cluster = get_cluster(*entry);
    if (cluster < 2) {
        cprintf("fat_read_file: invalid cluster: %d\n", cluster);
        return -1;
    }

    uint8_t cluster_buf[4096];  // Max cluster size we support
    if (bytes_per_cluster_ > sizeof(cluster_buf)) {
        cprintf("fat_read_file: cluster too large\n");
        return -1;
    }

    uint32_t bytes_read = 0;
    uint32_t skip_bytes = offset;

    while (cluster >= 2 && cluster < FAT32_EOC_MIN && bytes_read < size) {
        uint32_t sector = cluster_to_sector(cluster);
        if (dev_->read(partition_start_ + sector, cluster_buf, sectors_per_cluster_) != 0) {
            cprintf("fat_read_file: failed to read cluster %d\n", cluster);
            return -1;
        }

        // Calculate how much to copy from this cluster
        uint32_t cluster_offset = 0;
        uint32_t cluster_bytes = bytes_per_cluster_;

        // Skip bytes if needed
        if (skip_bytes > 0) {
            if (skip_bytes >= cluster_bytes) {
                skip_bytes -= cluster_bytes;
                cluster = read_entry(cluster);
                continue;
            }
            cluster_offset = skip_bytes;
            cluster_bytes -= skip_bytes;
            skip_bytes = 0;
        }

        // Copy data
        uint32_t to_copy = cluster_bytes;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }

        memcpy(buf + bytes_read, cluster_buf + cluster_offset, to_copy);
        bytes_read += to_copy;

        // Get next cluster
        cluster = read_entry(cluster);
    }

    return bytes_read;
}
