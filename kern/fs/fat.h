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
class FatInfo {
public: // TODO
    block_device_t* m_dev{};            // Block device
    uint32_t m_partition_start{};       // Partition start LBA (0 if no MBR)
    uint8_t  m_fat_type{};              // FAT type (12, 16, or 32)
    uint32_t m_bytes_per_sector{};      // Bytes per sector
    uint32_t m_sectors_per_cluster{};   // Sectors per cluster
    uint32_t m_bytes_per_cluster{};     // Bytes per cluster
    uint32_t m_reserved_sectors{};      // Reserved sectors
    uint32_t m_num_fats{};              // Number of FAT tables
    uint32_t m_root_entries{};          // Root directory entries (0 for FAT32)
    uint32_t m_fat_start{};             // FAT start sector (relative to partition)
    uint32_t m_fat_size{};              // FAT size in sectors
    uint32_t m_root_start{};            // Root directory start sector (FAT16 only)
    uint32_t m_root_sectors{};          // Root directory sectors (FAT16 only)
    uint32_t m_root_cluster{};          // Root directory cluster (FAT32 only)
    uint32_t m_data_start{};            // Data area start sector
    uint32_t m_cluster_count{};         // Total clusters
    uint32_t m_total_sectors{};         // Total sectors

    uint8_t m_buffer[512]{};            // Sector buffer
    uint32_t m_buffer_sector{};         // Buffered sector number
    bool m_buffer_dirty{};               // Buffer modified flag

    int mount(block_device_t* dev);
    void unmount();

    void print_info();

    uint32_t read_entry(uint32_t cluster);
    int write_entry(uint32_t cluster, uint32_t value);

    int read_root_dir(int (*callback)(fat_dir_entry_t* entry, void *arg), void *arg);
    int find_file(const char* filename, fat_dir_entry_t* entry);
    int read_file(fat_dir_entry_t *entry, uint8_t *buf, uint32_t offset, uint32_t size);

    uint32_t cluster_to_sector(uint32_t cluster);
    static void get_filename(fat_dir_entry_t* entry, char* buf, int bufsize);

private:
    static bool is_valid_entry(fat_dir_entry_t& entry);
    static uint32_t get_cluster(fat_dir_entry_t& entry);
};
