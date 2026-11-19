#pragma once

#include <debug/assert.h>
#include <sys/array.hpp>
#include <sys/inplace_vector.hpp>

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
    // Stable directory slot identity; open files borrow it while unlink is blocked.
    struct EntryLocation {
        uint32_t sector{};
        uint32_t index{};
        bool operator==(const EntryLocation&) const = default;
    };

    using EntryLocations = sys::inplace_vector<EntryLocation, 4>;

    struct DirectoryRecord {
        const FatDirEntry& entry;
        EntryLocation location;
        const char* long_name;
        const EntryLocations& long_locations;
        bool has_long_name;
    };

    class DirVisitor {
    public:
        virtual ~DirVisitor() = default;
        virtual int visit(const DirectoryRecord& record) = 0;
    };

    Error mount(BlockDevice* dev);
    Error unmount();
    Error sync();
    bool needs_sync() const { return needs_sync_; }

    void print() const;

    Result<int> read_dir(const char* relpath, DirVisitor& visitor);

    Result<int> read_file(FatDirEntry* entry, uint8_t* buf, uint32_t offset, uint32_t size);
    Result<int> write_file(const EntryLocation& location, const uint8_t* buf, uint32_t offset, uint32_t size,
                           bool append = false, size_t* end_offset = nullptr);
    Error truncate_file(const EntryLocation& location);
    Error read_file_entry(const EntryLocation& location, FatDirEntry* entry);

    Error find_file(const char* filename, FatDirEntry* result, EntryLocation* location = nullptr);

    Error mkdir(const char* relpath);
    Error create_file(const char* relpath);
    Error rename(const char* relpath, const char* new_name, bool preserve_location);
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
    Result<int> read_dir(uint32_t start_cluster, DirVisitor& visitor);

    Result<uint32_t> read_entry(uint32_t cluster);
    Error write_entry(uint32_t cluster, uint32_t value);
    Error load_fat_sector(uint32_t sector);
    Error flush_fat_buffer();
    Result<uint32_t> count_free_clusters();
    Error sync_fs_info();
    Error write_sectors(uint32_t start_lba, const void* buf, size_t block_count);

    Result<uint32_t> alloc_cluster();
    Error free_chain(uint32_t start_cluster);
    Error free_detached_chain();
    void rollback_new_cluster(uint32_t start_cluster);

    struct NameSlots {
        EntryLocations locations;
        bool unsupported_long_name{};
    };
    struct DirectorySpace {
        EntryLocations locations;
        EntryLocation end;
    };
    class DirectoryEdit;

    Error find_entry(uint32_t start_cluster, const char* name, FatDirEntry* out, EntryLocation* location = nullptr,
                     NameSlots* slots = nullptr);
    Error write_file_entry(const EntryLocation& location, const FatDirEntry& entry);
    // One write owns its cursor and sector scratch off the 4KB kernel stack.
    struct WriteState {
        EntryLocation location{};
        FatDirEntry entry{};
        const uint8_t* bytes{};
        uint32_t offset{};
        uint32_t end{};
        uint32_t position{};
        uint32_t cluster{};
        uint32_t tail{};
        uint32_t cluster_offset{};
        uint32_t done{};
        sys::array<uint8_t, BlockDevice::BLOCK_SIZE_BYTES> sector{};
    };
    Error transfer_file(WriteState& state);
    Error validate_file_chain(const FatDirEntry& entry);
    Error link_file_cluster(uint32_t tail, uint32_t old_link, uint32_t cluster);
    Error resolve_parent(const char* relpath, uint32_t* parent_cluster, char* child_name, size_t name_size);
    // On failure, report whether a partial write may have published the entry.
    Error add_dir_entry(uint32_t dir_cluster, const FatDirEntry* entry, bool* entry_may_exist = nullptr);
    Error add_named_entry(uint32_t dir_cluster, const FatDirEntry& entry, const char* long_name,
                          bool* entry_may_exist = nullptr);
    Result<DirectorySpace> reserve_dir_slots(uint32_t dir_cluster, size_t count);
    Error remove_dir_entry(const NameSlots& slots);
    Error prepare_name(uint32_t dir_cluster, const char* name, FatDirEntry& entry, bool& use_long_name,
                       EntryLocation source = EntryLocation{0, 0});

    void do_init_state(BlockDevice* dev, uint32_t partition_start, const Fat32BootSector& bs);

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
    uint32_t fs_info_sector_{};         // 0 when no usable FSInfo is present
    uint32_t free_clusters_{};          // Recounted from the FAT before first use
    bool fs_info_dirty_{};

    uint8_t buffer_[512]{};     // Sector buffer
    uint32_t buffer_sector_{};  // Buffered sector number
    bool buffer_dirty_{};       // Buffer modified flag
    bool needs_sync_{};
    // Detached file storage remains owned here until checked cleanup succeeds.
    uint32_t pending_free_cluster_{};
    uint32_t pending_free_next_{};
};
