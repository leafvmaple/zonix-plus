#pragma once

#include <base/types.h>
#include <base/bpb.h>
#include "../drivers/blk.h"

// FAT types
#define FAT_TYPE_FAT12  12
#define FAT_TYPE_FAT16  16
#define FAT_TYPE_FAT32  32

// FAT special cluster values
#define FAT16_FREE          0x0000
#define FAT16_RESERVED_MIN  0xFFF0
#define FAT16_BAD_CLUSTER   0xFFF7
#define FAT16_EOC_MIN       0xFFF8  // End of chain
#define FAT16_EOC_MAX       0xFFFF

// FAT12 special values
#define FAT12_FREE          0x000
#define FAT12_EOC_MIN       0xFF8
#define FAT12_EOC_MAX       0xFFF

// FAT32 special values
#define FAT32_FREE          0x00000000
#define FAT32_RESERVED_MIN  0x0FFFFFF0
#define FAT32_BAD_CLUSTER   0x0FFFFFF7
#define FAT32_EOC_MIN       0x0FFFFFF8
#define FAT32_EOC_MAX       0x0FFFFFFF
#define FAT32_CLUSTER_MASK  0x0FFFFFFF  // Mask for FAT32 entries (top 4 bits reserved)

// FAT file system information
typedef struct {
    block_device_t *dev;            // Block device
    uint32_t partition_start;       // Partition start LBA (0 if no MBR)
    uint8_t  fat_type;              // FAT type (12, 16, or 32)
    uint32_t bytes_per_sector;      // Bytes per sector
    uint32_t sectors_per_cluster;   // Sectors per cluster
    uint32_t bytes_per_cluster;     // Bytes per cluster
    uint32_t reserved_sectors;      // Reserved sectors
    uint32_t num_fats;              // Number of FAT tables
    uint32_t root_entries;          // Root directory entries (0 for FAT32)
    uint32_t fat_start;             // FAT start sector (relative to partition)
    uint32_t fat_size;              // FAT size in sectors
    uint32_t root_start;            // Root directory start sector (FAT16 only)
    uint32_t root_sectors;          // Root directory sectors (FAT16 only)
    uint32_t root_cluster;          // Root directory cluster (FAT32 only)
    uint32_t data_start;            // Data area start sector
    uint32_t cluster_count;         // Total clusters
    uint32_t total_sectors;         // Total sectors
    
    uint8_t *fat_buffer;            // FAT table cache (one sector)
    uint32_t fat_buffer_sector;     // Cached FAT sector number
    int fat_buffer_dirty;           // FAT buffer modified flag
} fat_info_t;

// FAT file system API
int fat_mount(block_device_t *dev, fat_info_t *info);
void fat_unmount(fat_info_t *info);

// FAT table operations
uint32_t fat_read_entry(fat_info_t *info, uint32_t cluster);
int fat_write_entry(fat_info_t *info, uint32_t cluster, uint32_t value);

// Directory operations
int fat_read_root_dir(fat_info_t *info, 
                      int (*callback)(fat_dir_entry_t *entry, void *arg),
                      void *arg);
int fat_find_file(fat_info_t *info, const char *filename, 
                  fat_dir_entry_t *entry);

// File operations
int fat_read_file(fat_info_t *info, fat_dir_entry_t *entry, 
                  void *buf, uint32_t offset, uint32_t size);

// Utility functions
void fat_get_filename(fat_dir_entry_t *entry, char *buf, int bufsize);
int fat_is_valid_entry(fat_dir_entry_t *entry);
uint32_t fat_cluster_to_sector(fat_info_t *info, uint32_t cluster);
uint32_t fat_get_cluster(fat_dir_entry_t *entry);
