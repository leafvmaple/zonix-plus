#pragma once

#include <base/types.h>
#include <base/bpb.h>
#include "block/blk.h"
#include "lib/result.h"

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

    Error mount(BlockDevice* dev);
    void unmount();

    void print() const;

    Result<int> read_dir(const char* relpath, DirVisitor& visitor);

    Result<int> read_file(FatDirEntry* entry, uint8_t* buf, uint32_t offset, uint32_t size);
    Result<int> write_file(FatDirEntry* entry, const uint8_t* buf, uint32_t offset, uint32_t size);

    Error find_file(const char* filename, FatDirEntry* result);

    Error mkdir(const char* relpath);
    Error create_file(const char* relpath);
    Error unlink(const char* relpath);
    Error rmdir(const char* relpath);

private:
    // Brent cycle detection uses constant space and one FAT read per link.
    class ClusterChain {
    public:
        ClusterChain(FatInfo& fat, uint32_t start) : fat_(fat), current_(start), checkpoint_(start) {}
        Result<uint32_t> next();  // 0 marks a validated end-of-chain.

    private:
        FatInfo& fat_;
        uint32_t current_{};
        uint32_t checkpoint_{};
        uint32_t power_{1};
        uint32_t distance_{};
        uint32_t visited_{};
        bool started_{};
        bool finished_{};
    };
    [[nodiscard]] bool valid_cluster(uint32_t cluster) const;
    Result<int> read_dir(uint32_t start_cluster, DirVisitor& visitor, bool verbose_read_error);

    Result<uint32_t> read_entry(uint32_t cluster);
    Error write_entry(uint32_t cluster, uint32_t value);

    Result<uint32_t> alloc_cluster();
    Error free_chain(uint32_t start_cluster);

    Error find_entry(uint32_t start_cluster, const char* name, FatDirEntry* out);
    Error resolve_parent(const char* relpath, uint32_t* parent_cluster, char* child_name, size_t name_size);
    Error add_dir_entry(uint32_t dir_cluster, const FatDirEntry* entry);
    Error remove_dir_entry(uint32_t dir_cluster, const char* name);
    void make_83_name(const char* name, char out_name[8], char out_ext[3]);

    void do_init_state(BlockDevice* dev, uint32_t partition_start, const Fat32BootSector& bs);
    Result<int> do_file_io(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size, const char* op,
                           bool writeback);

    [[nodiscard]] uint32_t cluster_to_sector(uint32_t cluster) const;

    BlockDevice* dev_{};                // Block device
    uint32_t partition_start_lba_{};    // Partition start LBA (0 if no MBR)
    uint8_t fat_type_{};                // FAT type (12, 16, or 32)
    uint32_t bytes_per_sector_{};       // Bytes per sector
    uint32_t sectors_per_cluster_{};    // Sectors per cluster
    uint32_t bytes_per_cluster_{};      // Bytes per cluster
    uint32_t reserved_sector_count_{};  // Reserved sectors
    uint32_t fat_count_{};              // Number of FAT tables
    uint32_t root_entry_count_{};       // Root directory entries (0 for FAT32)
    uint32_t fat_start_sector_{};       // FAT start sector (relative to partition)
    uint32_t fat_sector_count_{};       // FAT size in sectors
    uint32_t root_start_sector_{};      // Root directory start sector (FAT16 only)
    uint32_t root_sector_count_{};      // Root directory sectors (FAT16 only)
    uint32_t root_cluster_{};           // Root directory cluster (FAT32 only)
    uint32_t data_start_sector_{};      // Data area start sector
    uint32_t cluster_count_{};          // Total clusters
    uint32_t total_sector_count_{};     // Total sectors

    uint8_t buffer_[512]{};     // Sector buffer
    uint32_t buffer_sector_{};  // Buffered sector number
    bool buffer_dirty_{};       // Buffer modified flag
};
