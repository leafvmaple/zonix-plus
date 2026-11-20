#pragma once

#include "fs/fat.h"
#include "lib/memory.h"

namespace fat_test {

class FatImage : public BlockDevice {
public:
    explicit FatImage(uint8_t cluster_sectors = 16, uint32_t clusters = 4, uint8_t fat_copies = 1)
        : cluster_sectors_(cluster_sectors), clusters_(clusters), fat_copies_(fat_copies) {
        fat_sectors_ = ((clusters + 2) * sizeof(uint32_t) + BLOCK_SIZE_BYTES - 1) / BLOCK_SIZE_BYTES;
        block_count = data_start_lba() + clusters * cluster_sectors;
        data_ = static_cast<uint8_t*>(kmalloc(block_count * BLOCK_SIZE_BYTES));
        if (!data_) {
            return;
        }
        memset(data_, 0, block_count * BLOCK_SIZE_BYTES);
        auto& bs = boot_sector();
        bs.boot_signature_word = BOOT_SIGNATURE;
        bs.boot_signature = BPB_BOOT_SIGNATURE;
        bs.bytes_per_sector = BLOCK_SIZE_BYTES;
        bs.sectors_per_cluster = cluster_sectors;
        bs.reserved_sectors = 1;
        bs.num_fats = fat_copies;
        bs.total_sectors_32 = block_count;
        bs.fat_size_32 = fat_sectors_;
        bs.root_cluster = 2;
        set_link(2, fat::FAT32_EOC_MAX);
        set_link(3, fat::FAT32_EOC_MAX);
        set_link(4, fat::FAT32_EOC_MAX);
        set_link(5, fat::FAT32_EOC_MAX);
        auto* file = reinterpret_cast<FatDirEntry*>(cluster_data(2));
        memcpy(file->name, "FILE    ", 8);
        memcpy(file->ext, "TXT", 3);
        file->attr = FAT_ATTR_ARCHIVE;
        file->first_cluster_low = 3;
        file->file_size = cluster_bytes();
        for (size_t i = 0; i < cluster_bytes(); ++i) {
            cluster_data(3)[i] = static_cast<uint8_t>(i);
        }
    }
    ~FatImage() { kfree(data_); }
    FatImage(const FatImage&) = delete;
    FatImage& operator=(const FatImage&) = delete;
    bool ready() const { return data_ != nullptr; }
    size_t cluster_bytes() const { return cluster_sectors_ * BLOCK_SIZE_BYTES; }
    uint32_t data_start_lba() const { return 1 + fat_copies_ * fat_sectors_; }
    uint32_t cluster_lba(uint32_t cluster) const { return data_start_lba() + (cluster - 2) * cluster_sectors_; }
    uint32_t fat_lba(uint32_t cluster, uint32_t copy = 0) const {
        return 1 + copy * fat_sectors_ + cluster * sizeof(uint32_t) / BLOCK_SIZE_BYTES;
    }
    Fat32BootSector& boot_sector() { return *reinterpret_cast<Fat32BootSector*>(data_); }
    const Fat32BootSector& boot_sector() const { return *reinterpret_cast<const Fat32BootSector*>(data_); }
    uint8_t* cluster_data(uint32_t cluster) { return data_ + cluster_lba(cluster) * BLOCK_SIZE_BYTES; }
    void set_link(uint32_t cluster, uint32_t value) {
        for (uint32_t copy = 0; copy < fat_copies_; ++copy) {
            reinterpret_cast<uint32_t*>(data_ + (1 + copy * fat_sectors_) * BLOCK_SIZE_BYTES)[cluster] = value;
        }
    }
    uint32_t link(uint32_t cluster, uint32_t copy = 0) const {
        return reinterpret_cast<uint32_t*>(data_ + (1 + copy * fat_sectors_) * BLOCK_SIZE_BYTES)[cluster];
    }
    void clear_files() {
        memset(cluster_data(2), 0, cluster_bytes());
        for (uint32_t cluster = 3; cluster < clusters_ + 2; ++cluster) {
            set_link(cluster, fat::FAT32_FREE);
        }
    }
    uint32_t allocated_clusters() const {
        uint32_t count{};
        for (uint32_t cluster = 2; cluster < clusters_ + 2; ++cluster) {
            if (link(cluster) != fat::FAT32_FREE) {
                ++count;
            }
        }
        return count;
    }
    void fail_fat_reads(bool fail) { fail_fat_reads_ = fail; }
    void fail_read_at(uint32_t block, Error error) {
        read_block_ = block;
        read_error_ = error;
    }
    void fail_write_at(uint32_t block, size_t matching_write, Error error, size_t slot = 0, bool partial = false) {
        assert(slot < 2 && matching_write > 0);
        write_failures_[slot] = {block, matching_write, error, partial};
    }
    void fail_writes_at(uint32_t block, Error error) {
        write_block_ = block;
        write_error_ = error;
    }
    void set_write_hook(void (*hook)()) { write_hook_ = hook; }
    // Sectors read from the data region, where clusters live.
    size_t data_sectors_read() const { return data_sectors_read_; }
    Error flush_cache() override { return Error::None; }
    Error read_blocks(uint32_t block, void* buffer, size_t count) override {
        if (block > block_count || count > block_count - block) {
            return Error::Io;
        }
        if (fail_fat_reads_ && block == 1) {
            return Error::Io;
        }
        if (read_error_ != Error::None && block == read_block_) {
            return read_error_;
        }
        if (block + count > data_start_lba()) {
            data_sectors_read_ += block >= data_start_lba() ? count : block + count - data_start_lba();
        }
        memcpy(buffer, data_ + block * BLOCK_SIZE_BYTES, count * BLOCK_SIZE_BYTES);
        return Error::None;
    }
    Error write_blocks(uint32_t block, const void* buffer, size_t count) override {
        if (block > block_count || count > block_count - block) {
            return Error::Io;
        }
        if (write_error_ != Error::None && block == write_block_) {
            return write_error_;
        }
        for (auto& failure : write_failures_) {
            if (failure.error != Error::None && failure.block == block && --failure.remaining == 0) {
                Error error = failure.error;
                failure.error = Error::None;
                if (failure.partial) {
                    // Model a write which takes effect before reporting failure.
                    memcpy(data_ + block * BLOCK_SIZE_BYTES, buffer, count * BLOCK_SIZE_BYTES);
                }
                return error;
            }
        }
        memcpy(data_ + block * BLOCK_SIZE_BYTES, buffer, count * BLOCK_SIZE_BYTES);
        if (write_hook_) {
            write_hook_();
        }
        return Error::None;
    }

private:
    struct WriteFailure {
        uint32_t block{};
        size_t remaining{};
        Error error{Error::None};
        bool partial{};
    };
    WriteFailure write_failures_[2]{};
    size_t data_sectors_read_{};
    uint32_t read_block_{};
    uint32_t write_block_{};
    Error read_error_{Error::None};
    Error write_error_{Error::None};
    void (*write_hook_)(){};
    uint8_t* data_{};
    uint8_t cluster_sectors_{};
    uint32_t clusters_{};
    uint8_t fat_copies_{};
    uint32_t fat_sectors_{};
    bool fail_fat_reads_{};
};

}  // namespace fat_test
