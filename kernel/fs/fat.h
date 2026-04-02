#pragma once

#include <base/types.h>
#include <base/bpb.h>
#include "block/blk.h"

namespace fat {

inline constexpr int TYPE_FAT12 = 12;
inline constexpr int TYPE_FAT16 = 16;
inline constexpr int TYPE_FAT32 = 32;

inline constexpr uint32_t FAT32_FREE = 0x00000000;
inline constexpr uint32_t FAT32_RESERVED_MIN = 0x0FFFFFF0;
inline constexpr uint32_t FAT32_BAD_CLUSTER = 0x0FFFFFF7;
inline constexpr uint32_t FAT32_EOC_MIN = 0x0FFFFFF8;
inline constexpr uint32_t FAT32_EOC_MAX = 0x0FFFFFFF;
inline constexpr uint32_t FAT32_CLUSTER_MASK = 0x0FFFFFFF;  // Mask for FAT32 entries (top 4 bits reserved)

}  // namespace fat

class FatInfo {
public:
    class DirVisitor {
    public:
        virtual ~DirVisitor() = default;
        virtual int visit(FatDirEntry* entry) = 0;
    };

    int mount(BlockDevice* dev);
    void unmount();

    void print() const;

    int read_dir(const char* relpath, DirVisitor& visitor);

    int read_file(FatDirEntry* entry, uint8_t* buf, uint32_t offset, uint32_t size);
    int write_file(FatDirEntry* entry, const uint8_t* buf, uint32_t offset, uint32_t size);

    int find_file(const char* filename, FatDirEntry* result);

private:
    int read_dir(uint32_t start_cluster, DirVisitor& visitor, bool verbose_read_error);

    uint32_t read_entry(uint32_t cluster);
    int write_entry(uint32_t cluster, uint32_t value);

    bool find_entry(uint32_t start_cluster, const char* name, FatDirEntry* out);

    void do_init_state(BlockDevice* dev, uint32_t partition_start, const Fat32BootSector& bs);
    int do_file_io(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size, const char* op, bool writeback);

    [[nodiscard]] uint32_t cluster_to_sector(uint32_t cluster) const;

    BlockDevice* dev_{};              // Block device
    uint32_t partition_start_{};      // Partition start LBA (0 if no MBR)
    uint8_t fat_type_{};              // FAT type (12, 16, or 32)
    uint32_t bytes_per_sector_{};     // Bytes per sector
    uint32_t sectors_per_cluster_{};  // Sectors per cluster
    uint32_t bytes_per_cluster_{};    // Bytes per cluster
    uint32_t reserved_sectors_{};     // Reserved sectors
    uint32_t num_fats_{};             // Number of FAT tables
    uint32_t root_entries_{};         // Root directory entries (0 for FAT32)
    uint32_t fat_start_{};            // FAT start sector (relative to partition)
    uint32_t fat_size_{};             // FAT size in sectors
    uint32_t root_start_{};           // Root directory start sector (FAT16 only)
    uint32_t root_sectors_{};         // Root directory sectors (FAT16 only)
    uint32_t root_cluster_{};         // Root directory cluster (FAT32 only)
    uint32_t data_start_{};           // Data area start sector
    uint32_t cluster_count_{};        // Total clusters
    uint32_t total_sectors_{};        // Total sectors

    uint8_t buffer_[512]{};     // Sector buffer
    uint32_t buffer_sector_{};  // Buffered sector number
    bool buffer_dirty_{};       // Buffer modified flag
};
