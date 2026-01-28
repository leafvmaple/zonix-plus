#include "fat.h"
#include "stdio.h"
#include "../include/memory.h"
#include "../include/string.h"
#include <base/mbr.h>
#include <base/bpb.h>

// Static buffer for FAT sector caching
static uint8_t fat_sector_buffer[512];

/**
 * Read boot sector and initialize FAT file system info
 */
int fat_mount(block_device_t *dev, fat_info_t *info) {
    if (!dev || !info) {
        return -1;
    }
    
    uint32_t partition_start = 0;  // LBA offset for partition
    
    // Read sector 0 to check if it's MBR or VBR
    uint8_t sector0[512];
    if (blk_read(dev, 0, sector0, 1) != 0) {
        cprintf("fat_mount: failed to read sector 0\n");
        return -1;
    }
    
    // Check signature
    uint16_t signature = *(uint16_t*)(sector0 + 510);
    if (signature != 0xAA55) {
        cprintf("fat_mount: invalid signature: 0x%04x\n", signature);
        return -1;
    }
    
    // Check if it's MBR by looking for partition table
    mbr_t *mbr = (mbr_t*)sector0;
    
    // Check if first partition entry looks valid (has a known partition type)
    if (mbr->partitions[0].partition_type == PART_TYPE_FAT16 ||
        mbr->partitions[0].partition_type == PART_TYPE_FAT16_LBA ||
        mbr->partitions[0].partition_type == PART_TYPE_FAT16_SMALL ||
        mbr->partitions[0].partition_type == PART_TYPE_FAT32_LBA ||
        mbr->partitions[0].partition_type == PART_TYPE_FAT32 ||
        mbr->partitions[0].partition_type == PART_TYPE_FAT12) {
        // This looks like an MBR, use first partition
        partition_start = mbr->partitions[0].start_lba;
        cprintf("fat_mount: MBR detected, partition starts at LBA %d\n", partition_start);
    } else {
        // No MBR, assume sector 0 is VBR
        partition_start = 0;
    }
    
    // Read boot sector (VBR) from partition start
    struct fat32_boot_sector bs;
    if (blk_read(dev, partition_start, &bs, 1) != 0) {
        cprintf("fat_mount: failed to read boot sector at LBA %d\n", partition_start);
        return -1;
    }
    
    // Check boot signature
    if (bs.boot_signature_word != 0xAA55) {
        cprintf("fat_mount: invalid boot signature: 0x%04x\n", bs.boot_signature_word);
        return -1;
    }
    
    // Initialize FAT info
    memset(info, 0, sizeof(fat_info_t));
    info->dev = dev;
    info->partition_start = partition_start;
    info->bytes_per_sector = bs.bytes_per_sector;
    info->sectors_per_cluster = bs.sectors_per_cluster;
    info->bytes_per_cluster = bs.bytes_per_sector * bs.sectors_per_cluster;
    info->reserved_sectors = bs.reserved_sectors;
    info->num_fats = bs.num_fats;
    info->fat_size = bs.fat_size_32;
    info->root_entries = 0;
    info->root_cluster = bs.root_cluster;
    
    if (bs.total_sectors_32 != 0) {
        info->total_sectors = bs.total_sectors_32;
    } else {
        cprintf("fat_mount: invalid FAT32 total sectors\n");
        return -1;
    }
    
    // Calculate FAT start sector (relative to partition)
    info->fat_start = info->reserved_sectors;
    info->root_start = 0;
    info->root_sectors = 0;
    info->data_start = info->fat_start + (info->num_fats * info->fat_size);
    
    // Calculate cluster count
    uint32_t data_sectors = info->total_sectors - info->data_start;
    info->cluster_count = data_sectors / info->sectors_per_cluster;
    info->fat_type = FAT_TYPE_FAT32;
    
    // Initialize FAT cache
    info->fat_buffer = fat_sector_buffer;
    info->fat_buffer_sector = (uint32_t)-1;  // Invalid sector
    info->fat_buffer_dirty = 0;
    
    // Print file system information
    char oem[9] = {0};
    memcpy(oem, bs.oem, 8);
    char label[12] = {0};
    memcpy(label, bs.volume_label, 11);
    char fstype[9] = {0};
    memcpy(fstype, bs.fs_type, 8);
    
    cprintf("FAT%d mounted: %s\n", info->fat_type, label);
    cprintf("  OEM: %s\n", oem);
    cprintf("  Partition Start: LBA %d\n", info->partition_start);
    cprintf("  Bytes/Sector: %d\n", info->bytes_per_sector);
    cprintf("  Sectors/Cluster: %d\n", info->sectors_per_cluster);
    cprintf("  Total Sectors: %d\n", info->total_sectors);
    cprintf("  FAT Start: %d (absolute LBA: %d)\n", info->fat_start, 
            info->partition_start + info->fat_start);
    cprintf("  FAT Size: %d sectors\n", info->fat_size);
    cprintf("  Root Cluster: %d\n", info->root_cluster);
    cprintf("  Data Start: %d\n", info->data_start);
    cprintf("  Cluster Count: %d\n", info->cluster_count);
    
    return 0;
}

/**
 * Unmount FAT file system
 */
void fat_unmount(fat_info_t *info) {
    if (!info) {
        return;
    }
    
    // Flush FAT cache if dirty
    if (info->fat_buffer_dirty) {
        // TODO: Write back FAT buffer
        info->fat_buffer_dirty = 0;
    }
    
    memset(info, 0, sizeof(fat_info_t));
}

/**
 * Get cluster number from directory entry (handles both FAT16 and FAT32)
 */
uint32_t fat_get_cluster(fat_dir_entry_t *entry) {
    if (!entry) {
        return 0;
    }
    
    uint32_t cluster = entry->first_cluster_lo;
    cluster |= ((uint32_t)entry->first_cluster_hi) << 16;
    
    return cluster;
}

/**
 * Read FAT entry for a cluster
 */
uint32_t fat_read_entry(fat_info_t *info, uint32_t cluster) {
    if (!info || cluster < 2 || cluster >= info->cluster_count + 2) {
        return 0;
    }
    
    if (info->fat_type != FAT_TYPE_FAT32) {
        return 0;
    }
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = info->fat_start + (fat_offset / info->bytes_per_sector);
    uint32_t ent_offset = fat_offset % info->bytes_per_sector;
    
    // Load FAT sector if not cached
    if (fat_sector != info->fat_buffer_sector) {
        if (blk_read(info->dev, info->partition_start + fat_sector, info->fat_buffer, 1) != 0) {
            return 0;
        }
        info->fat_buffer_sector = fat_sector;
    }
    
    uint32_t value = *(uint32_t *)&info->fat_buffer[ent_offset];
    return value & FAT32_CLUSTER_MASK;  // Mask out top 4 bits
}

/**
 * Write FAT entry for a cluster (for future write support)
 */
int fat_write_entry(fat_info_t *info, uint32_t cluster, uint32_t value) {
    // TODO: Implement FAT write support
    (void)info;
    (void)cluster;
    (void)value;
    return -1;
}

/**
 * Convert cluster number to sector number
 */
uint32_t fat_cluster_to_sector(fat_info_t *info, uint32_t cluster) {
    if (!info || cluster < 2) {
        return 0;
    }
    
    return info->data_start + ((cluster - 2) * info->sectors_per_cluster);
}

/**
 * Check if directory entry is valid
 */
int fat_is_valid_entry(fat_dir_entry_t *entry) {
    if (!entry) {
        return 0;
    }
    
    // Check for end of directory or deleted entry
    if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
        return 0;
    }
    
    // Check for volume label or LFN entries (we skip these for now)
    if ((entry->attr & FAT_ATTR_VOLUME_ID) || (entry->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
        return 0;
    }
    
    return 1;
}

/**
 * Get filename from directory entry (8.3 format)
 */
void fat_get_filename(fat_dir_entry_t *entry, char *buf, int bufsize) {
    if (!entry || !buf || bufsize < 13) {
        return;
    }
    
    int i, j = 0;
    
    // Copy filename (8 chars)
    for (i = 0; i < 8 && entry->name[i] != ' '; i++) {
        buf[j++] = entry->name[i];
    }
    
    // Add extension if present
    if (entry->ext[0] != ' ') {
        buf[j++] = '.';
        for (i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            buf[j++] = entry->ext[i];
        }
    }
    
    buf[j] = '\0';
}

/**
 * Read root directory and call callback for each entry
 */
int fat_read_root_dir(fat_info_t *info, 
                      int (*callback)(fat_dir_entry_t *entry, void *arg),
                      void *arg) {
    if (!info || !callback) {
        return -1;
    }
    
    int count = 0;

    if (info->fat_type != FAT_TYPE_FAT32) {
        return -1;
    }
    
    uint32_t cluster = info->root_cluster;
    
    uint8_t sector_buf[512];
    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        // Read all sectors in this cluster
        uint32_t sector = fat_cluster_to_sector(info, cluster);
        
        for (uint32_t i = 0; i < info->sectors_per_cluster; i++, sector++) {
            if (blk_read(info->dev, info->partition_start + sector, sector_buf, 1) != 0) {
                cprintf("fat_read_root_dir: failed to read sector %d\n", sector);
                return -1;
            }
            
            // Process each directory entry in this sector
            fat_dir_entry_t *entries = (fat_dir_entry_t *)sector_buf;
            for (int j = 0; j < info->bytes_per_sector / 32; j++) {
                fat_dir_entry_t *entry = &entries[j];
                
                // Check for end of directory
                if (entry->name[0] == 0x00) {
                    return count;
                }
                
                // Skip invalid entries
                if (!fat_is_valid_entry(entry)) {
                    continue;
                }
                
                // Call callback
                if (callback(entry, arg) != 0) {
                    return count;
                }
                
                count++;
            }
        }
        
        // Get next cluster in chain
        cluster = fat_read_entry(info, cluster);
    }
    
    return count;
}

/**
 * Find a file in root directory by name
 */
int fat_find_file(fat_info_t *info, const char *filename, 
                  fat_dir_entry_t *result) {
    if (!info || !filename || !result) {
        return -1;
    }
    
    // Convert filename to 8.3 format
    char name[8] = "        ";
    char ext[3] = "   ";
    
    const char *dot = strchr(filename, '.');
    if (dot) {
        int name_len = dot - filename;
        if (name_len > 8) name_len = 8;
        memcpy(name, filename, name_len);
        
        int ext_len = strlen(dot + 1);
        if (ext_len > 3) ext_len = 3;
        memcpy(ext, dot + 1, ext_len);
    } else {
        int name_len = strlen(filename);
        if (name_len > 8) name_len = 8;
        memcpy(name, filename, name_len);
    }
    
    // Convert to uppercase
    for (int i = 0; i < 8; i++) {
        if (name[i] >= 'a' && name[i] <= 'z') {
            name[i] -= 32;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (ext[i] >= 'a' && ext[i] <= 'z') {
            ext[i] -= 32;
        }
    }
    
    if (info->fat_type != FAT_TYPE_FAT32) {
        return -1;
    }
    // FAT32: Root directory is a cluster chain
    uint32_t cluster = info->root_cluster;
    
    uint8_t sector_buf[512];
    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint32_t sector = fat_cluster_to_sector(info, cluster);
        
        for (uint32_t s = 0; s < info->sectors_per_cluster; s++, sector++) {
            if (blk_read(info->dev, info->partition_start + sector, sector_buf, 1) != 0) {
                return -1;
            }
            
            fat_dir_entry_t *entries = (fat_dir_entry_t *)sector_buf;
            for (int j = 0; j < info->bytes_per_sector / 32; j++) {
                fat_dir_entry_t *entry = &entries[j];
                
                if (entry->name[0] == 0x00) {
                    return -1;  // End of directory
                }
                
                if (!fat_is_valid_entry(entry)) {
                    continue;
                }
                
                // Compare name and extension
                if (memcmp(entry->name, name, 8) == 0 && 
                    memcmp(entry->ext, ext, 3) == 0) {
                    memcpy(result, entry, sizeof(fat_dir_entry_t));
                    return 0;
                }
            }
        }
        
        // Get next cluster
        cluster = fat_read_entry(info, cluster);
    }
    
    return -1;  // Not found
}

/**
 * Read file data from a file entry
 */
int fat_read_file(fat_info_t *info, fat_dir_entry_t *entry, 
                  void *buf, uint32_t offset, uint32_t size) {
    if (!info || !entry || !buf) {
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
    uint32_t cluster = fat_get_cluster(entry);
    if (cluster < 2) {
        cprintf("fat_read_file: invalid cluster: %d\n", cluster);
        return -1;
    }
    
    uint8_t cluster_buf[4096];  // Max cluster size we support
    if (info->bytes_per_cluster > sizeof(cluster_buf)) {
        cprintf("fat_read_file: cluster too large\n");
        return -1;
    }
    
    uint32_t bytes_read = 0;
    uint32_t skip_bytes = offset;
    
    // Traverse cluster chain
    while (cluster >= 2 && cluster < FAT32_EOC_MIN && bytes_read < size) {
        // Read cluster
        uint32_t sector = fat_cluster_to_sector(info, cluster);
        if (blk_read(info->dev, info->partition_start + sector, cluster_buf, info->sectors_per_cluster) != 0) {
            cprintf("fat_read_file: failed to read cluster %d\n", cluster);
            return -1;
        }
        
        // Calculate how much to copy from this cluster
        uint32_t cluster_offset = 0;
        uint32_t cluster_bytes = info->bytes_per_cluster;
        
        // Skip bytes if needed
        if (skip_bytes > 0) {
            if (skip_bytes >= cluster_bytes) {
                skip_bytes -= cluster_bytes;
                cluster = fat_read_entry(info, cluster);
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
        
        memcpy((uint8_t *)buf + bytes_read, cluster_buf + cluster_offset, to_copy);
        bytes_read += to_copy;
        
        // Get next cluster
        cluster = fat_read_entry(info, cluster);
    }
    
    return bytes_read;
}
