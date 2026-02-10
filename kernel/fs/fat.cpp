#include "fat.h"
#include "stdio.h"
#include "math.h"
#include "../include/memory.h"
#include "../include/string.h"
#include <base/mbr.h>
#include <base/bpb.h>

int FatInfo::mount(BlockDevice* dev) {
    if (!dev) {
        return -1;
    }

    mbr_t mbr{};
    uint32_t partitionStart{};

    if (dev->read(0, &mbr, 1) != 0) {
        cprintf("fat_mount: failed to read sector 0\n");
        return -1;
    }

    if (mbr.signature != 0xAA55) {
        cprintf("fat_mount: invalid signature: 0x%04x\n", mbr.signature);
        return -1;
    }

    if (mbr.partitions[0].type == PART_TYPE_FAT32_LBA || mbr.partitions[0].type == PART_TYPE_FAT32) {
        partitionStart = mbr.partitions[0].start_lba;
        cprintf("fat_mount: MBR detected, partition starts at LBA %d\n", partitionStart);
    } else {
        // No MBR, assume sector 0 is VBR
        partitionStart = 0;
    }

    struct fat32_boot_sector bs{};
    if (dev->read(partitionStart, &bs, 1) != 0) {
        cprintf("fat_mount: failed to read boot sector at LBA %d\n", partitionStart);
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

    m_dev = dev;
    m_partition_start = partitionStart;
    m_total_sectors = bs.total_sectors_32;
    m_bytes_per_sector = bs.bytes_per_sector;
    m_sectors_per_cluster = bs.sectors_per_cluster;
    m_bytes_per_cluster = bs.bytes_per_sector * bs.sectors_per_cluster;
    
    m_reserved_sectors = bs.reserved_sectors;
    m_num_fats = bs.num_fats;
    m_fat_size = bs.fat_size_32;
    m_root_entries = 0;
    m_root_cluster = bs.root_cluster;
    
    // Calculate FAT start sector (relative to partition)
    m_root_start = 0;
    m_root_sectors = 0;
    m_fat_start = m_reserved_sectors;
    m_data_start = m_fat_start + (m_num_fats * m_fat_size);
    
    // Calculate cluster count
    uint32_t dataSectors = m_total_sectors - m_data_start;
    m_cluster_count = dataSectors / m_sectors_per_cluster;
    m_fat_type = FAT_TYPE_FAT32;
    
    // Initialize FAT cache
    m_buffer_sector = (uint32_t)-1;  // Invalid sector
    m_buffer_dirty = false;
    
    // Print file system information
    char oem[9]{};
    char label[12]{};

    memcpy(oem, bs.oem, 8);
    memcpy(label, bs.volume_label, 11);
    
    cprintf("FAT%d mounted: %s\n", m_fat_type, label);
    cprintf("  OEM: %s\n", oem);
    cprintf("  Partition Start: LBA %d\n", partitionStart);
    print_info();
    
    return 0;
}

void FatInfo::unmount() {
    // Flush FAT buffer if dirty
    if (m_buffer_dirty && m_buffer_sector != (uint32_t)-1) {
        for (uint32_t i = 0; i < m_num_fats; i++) {
            uint32_t fatSector = m_fat_start + (i * m_fat_size) + (m_buffer_sector - m_fat_start);
            if (m_dev->write(m_partition_start + fatSector, m_buffer, 1) != 0) {
                cprintf("fat_unmount: failed to write FAT sector %d\n", fatSector);
            }
        }
        m_buffer_dirty = false;
    }
    
    // Clear info
    m_dev = nullptr;
    m_buffer_sector = (uint32_t)-1;
    m_buffer_dirty = 0;
}

void FatInfo::print_info() {
    cprintf("FAT%d File System Information:\n", m_fat_type);
    cprintf("  Bytes/Sector: %d\n", m_bytes_per_sector);
    cprintf("  Sectors/Cluster: %d\n", m_sectors_per_cluster);
    cprintf("  Bytes/Cluster: %d\n", m_bytes_per_cluster);
    cprintf("  Total Sectors: %d\n", m_total_sectors);
    cprintf("  FAT Start: sector %d\n", m_fat_start);
    cprintf("  FAT Size: %d sectors\n", m_fat_size);
    if (m_fat_type == FAT_TYPE_FAT32) {
        cprintf("  Root Cluster: %d\n", m_root_cluster);
    } else {
        cprintf("  Root Start: sector %d\n", m_root_start);
        cprintf("  Root Entries: %d\n", m_root_entries);
    }
    cprintf("  Data Start: sector %d\n", m_data_start);
    cprintf("  Cluster Count: %d\n", m_cluster_count);
}

uint32_t FatInfo::get_cluster(fat_dir_entry_t& entry) {
    uint32_t cluster = entry.first_cluster_low;
    cluster |= ((uint32_t)entry.first_cluster_high) << 16;
    
    return cluster;
}

uint32_t FatInfo::read_entry(uint32_t cluster) {
    if (cluster < 2 || cluster >= m_cluster_count + 2) {
        return 0;
    }
    
    if (m_fat_type != FAT_TYPE_FAT32) {
        return 0;
    }
    uint32_t fatOffset = cluster << 2;
    uint32_t fatSector = m_fat_start + (fatOffset / m_bytes_per_sector);
    uint32_t entOffset = fatOffset % m_bytes_per_sector;
    
    // Load FAT sector if not cached
    if (fatSector != m_buffer_sector) {
        if (m_dev->read(m_partition_start + fatSector, m_buffer, 1) != 0) {
            return 0;
        }
        m_buffer_sector = fatSector;
    }
    
    uint32_t value = *(uint32_t *)&m_buffer[entOffset];
    return value & FAT32_CLUSTER_MASK;  // Mask out top 4 bits
}

int FatInfo::write_entry(uint32_t cluster, uint32_t value) {
    // TODO: Implement FAT write support
    (void)cluster;
    (void)value;
    return -1;
}

uint32_t FatInfo::cluster_to_sector(uint32_t cluster) {
    if (cluster < 2) {
        return 0;
    }
    
    return m_data_start + ((cluster - 2) * m_sectors_per_cluster);
}

bool FatInfo::is_valid_entry(fat_dir_entry_t& entry) {
    if (entry.name[0] == 0x00 || entry.name[0] == 0xE5) {
        return 0;
    }

    if ((entry.attr & FAT_ATTR_VOLUME_ID) || (entry.attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
        return 0;
    }
    
    return 1;
}

void FatInfo::get_filename(fat_dir_entry_t *entry, char* buf, int bufsize) {
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

int FatInfo::read_root_dir(int (*callback)(fat_dir_entry_t* entry, void *arg), void* arg) {
    if (!callback) {
        return -1;
    }
    
    int count{};

    if (m_fat_type != FAT_TYPE_FAT32) {
        return -1;
    }
    
    uint8_t sectorBuf[512];
    for (uint32_t cluster = m_root_cluster;
        cluster >= 2 && cluster < FAT32_EOC_MIN; cluster = read_entry(cluster)) {

        uint32_t sector = cluster_to_sector(cluster);
        
        for (uint32_t i = 0; i < m_sectors_per_cluster; i++) {
            if (m_dev->read(m_partition_start + sector + i, sectorBuf, 1) != 0) {
                cprintf("fat_read_root_dir: failed to read sector %d\n", sector + i);
                return -1;
            }

            fat_dir_entry_t* entries = (fat_dir_entry_t*)sectorBuf;
            for (uint32_t j = 0; j < m_bytes_per_sector / 32; j++) {
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

int FatInfo::find_file(const char* filename, fat_dir_entry_t *result) {
    if (!filename || !result) {
        return -1;
    }   
    if (m_fat_type != FAT_TYPE_FAT32) {
        return -1;
    }
    
    // Convert filename to 8.3 format
    char name[8]{};
    char ext[3]{};
    memset(name, ' ', sizeof(name));
    memset(ext, ' ', sizeof(ext));
    
    const char *dot = strchr(filename, '.');
    if (dot) {
        memcpy(name, filename, min(dot - filename, (long)8));
        memcpy(ext, dot + 1, min(strlen(dot + 1), (size_t)3));
    } else {
        memcpy(name, filename, min(strlen(filename), (size_t)8));
    }

    for (char &c : name) {
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
    }
    for (char &c : ext) {
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
    }

    uint8_t sectorBuf[512];
    for (uint32_t cluster = m_root_cluster;
        cluster >= 2 && cluster < FAT32_EOC_MIN; cluster = read_entry(cluster)) {

        uint32_t sector = cluster_to_sector(cluster);
        
        for (uint32_t s = 0; s < m_sectors_per_cluster; s++, sector++) {
            if (m_dev->read(m_partition_start + sector, sectorBuf, 1) != 0) {
                return -1;
            }
            
            fat_dir_entry_t* entries = (fat_dir_entry_t*)sectorBuf;
            for (uint32_t j = 0; j < m_bytes_per_sector / 32; j++) {
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
    
    uint8_t clusterBuf[4096];  // Max cluster size we support
    if (m_bytes_per_cluster > sizeof(clusterBuf)) {
        cprintf("fat_read_file: cluster too large\n");
        return -1;
    }
    
    uint32_t bytesRead = 0;
    uint32_t skipBytes = offset;

    while (cluster >= 2 && cluster < FAT32_EOC_MIN && bytesRead < size) {

        uint32_t sector = cluster_to_sector(cluster);
        if (m_dev->read(m_partition_start + sector, clusterBuf, m_sectors_per_cluster) != 0) {
            cprintf("fat_read_file: failed to read cluster %d\n", cluster);
            return -1;
        }
        
        // Calculate how much to copy from this cluster
        uint32_t clusterOffset = 0;
        uint32_t clusterBytes = m_bytes_per_cluster;
        
        // Skip bytes if needed
        if (skipBytes > 0) {
            if (skipBytes >= clusterBytes) {
                skipBytes -= clusterBytes;
                cluster = read_entry(cluster);
                continue;
            }
            clusterOffset = skipBytes;
            clusterBytes -= skipBytes;
            skipBytes = 0;
        }
        
        // Copy data
        uint32_t toCopy = clusterBytes;
        if (toCopy > size - bytesRead) {
            toCopy = size - bytesRead;
        }
        
        memcpy(buf + bytesRead, clusterBuf + clusterOffset, toCopy);
        bytesRead += toCopy;
        
        // Get next cluster
        cluster = read_entry(cluster);
    }
    
    return bytesRead;
}
