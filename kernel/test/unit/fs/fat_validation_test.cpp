#include "test/test_defs.h"
#include "fs/fat.h"
#include "lib/memory.h"
#include "drivers/intr.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

class FatImage : public BlockDevice {
public:
    explicit FatImage(uint8_t cluster_sectors = 16) : cluster_sectors_(cluster_sectors) {
        block_count = 2 + 4 * cluster_sectors;
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
        bs.num_fats = 1;
        bs.total_sectors_32 = block_count;
        bs.fat_size_32 = 1;
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
    Fat32BootSector& boot_sector() { return *reinterpret_cast<Fat32BootSector*>(data_); }
    uint8_t* cluster_data(uint32_t cluster) {
        return data_ + (2 + (cluster - 2) * cluster_sectors_) * BLOCK_SIZE_BYTES;
    }
    void set_link(uint32_t cluster, uint32_t value) {
        reinterpret_cast<uint32_t*>(data_ + BLOCK_SIZE_BYTES)[cluster] = value;
    }
    uint32_t link(uint32_t cluster) const { return reinterpret_cast<uint32_t*>(data_ + BLOCK_SIZE_BYTES)[cluster]; }
    void fail_fat_reads(bool fail) { fail_fat_reads_ = fail; }
    Error read(uint32_t block, void* buffer, size_t count) override {
        if (block > block_count || count > block_count - block) {
            return Error::Io;
        }
        if (fail_fat_reads_ && block == 1) {
            return Error::Io;
        }
        memcpy(buffer, data_ + block * BLOCK_SIZE_BYTES, count * BLOCK_SIZE_BYTES);
        return Error::None;
    }
    Error write(uint32_t block, const void* buffer, size_t count) override {
        if (block > block_count || count > block_count - block) {
            return Error::Io;
        }
        memcpy(data_ + block * BLOCK_SIZE_BYTES, buffer, count * BLOCK_SIZE_BYTES);
        return Error::None;
    }

private:
    uint8_t* data_{};
    uint8_t cluster_sectors_{};
    bool fail_fat_reads_{};
};

class IgnoreEntries : public FatInfo::DirVisitor {
public:
    int visit(FatDirEntry*) override { return 0; }
};

void test_geometry() {
    TEST_START("FAT mount rejects malformed or unsupported BPB geometry");
    FatImage image;
    FatInfo fs;
    TEST_ASSERT(image.ready(), "Allocated heap-backed disk image");
    if (!image.ready()) {
        TEST_END();
        return;
    }
    auto& bs = image.boot_sector();
    bs.sectors_per_cluster = 0;
    TEST_ASSERT(fs.mount(&image) == Error::BadFs, "Zero cluster size rejected before division");
    bs.sectors_per_cluster = 3;
    TEST_ASSERT(fs.mount(&image) == Error::BadFs, "Non-power-of-two cluster size rejected");
    bs.sectors_per_cluster = 16;
    bs.bytes_per_sector = 0;
    TEST_ASSERT(fs.mount(&image) == Error::BadFs, "Zero sector size rejected");
    for (uint16_t bytes = 1024; bytes <= 4096; bytes *= 2) {
        bs.bytes_per_sector = bytes;
        TEST_ASSERT(fs.mount(&image) == Error::NotSupported, "Larger logical sector rejected without buffer overflow");
    }
    bs.bytes_per_sector = 512;
    bs.total_sectors_32 = 1;
    TEST_ASSERT(fs.mount(&image) == Error::BadFs, "Metadata larger than volume rejected");
    bs.total_sectors_32 = image.block_count + 1;
    TEST_ASSERT(fs.mount(&image) == Error::BadFs, "Volume beyond device extent rejected");
    bs.total_sectors_32 = image.block_count;
    bs.root_cluster = 6;
    TEST_ASSERT(fs.mount(&image) == Error::BadFs, "Out-of-range root cluster rejected");
    bs.root_cluster = 2;
    TEST_ASSERT(fs.mount(&image) == Error::None, "Valid 8KB-cluster volume still mounts");
    fs.unmount();
    TEST_END();
}

void test_large_cluster_io() {
    TEST_START("FAT reads and writes full 8KB clusters from a 4KB kernel stack");
    FatImage image;
    FatInfo fs;
    if (!image.ready()) {
        TEST_ASSERT(false, "Allocated image");
        TEST_END();
        return;
    }
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted large-cluster fixture");
    FatDirEntry file{};
    TEST_ASSERT(fs.find_file("FILE.TXT", &file) == Error::None, "Found file");
    auto* bytes = static_cast<uint8_t*>(kmalloc(image.cluster_bytes()));
    if (bytes) {
        auto result = fs.read_file(&file, bytes, 0, file.file_size);
        bool intact = result.ok() && result.value() == static_cast<int>(image.cluster_bytes());
        for (size_t i = 0; i < image.cluster_bytes(); ++i) {
            intact = intact && bytes[i] == static_cast<uint8_t>(i);
        }
        TEST_ASSERT(intact, "Full cluster read preserves every byte");
        memset(bytes, 0xA5, image.cluster_bytes());
        auto written = fs.write_file(&file, bytes, 0, file.file_size);
        TEST_ASSERT(written.ok() && written.value() == static_cast<int>(image.cluster_bytes()) &&
                        image.cluster_data(3)[image.cluster_bytes() - 1] == 0xA5,
                    "Full cluster write reaches its last byte");
        auto eof = fs.read_file(&file, bytes, file.file_size, 1);
        TEST_ASSERT(eof.ok() && eof.value() == 0, "Read at EOF returns zero");
        kfree(bytes);
    } else {
        TEST_ASSERT(false, "Allocated I/O buffer");
    }
    fs.unmount();
    TEST_END();
}

void test_directory_chains() {
    TEST_START("Cyclic and invalid directory links terminate with errors");
    FatImage image(1);
    FatInfo fs;
    IgnoreEntries visitor;
    if (!image.ready()) {
        TEST_ASSERT(false, "Allocated image");
        TEST_END();
        return;
    }
    memset(image.cluster_data(2), 0xE5, image.cluster_bytes());
    memset(image.cluster_data(3), 0xE5, image.cluster_bytes());
    image.set_link(2, 3);
    image.set_link(3, 2);
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted cyclic directory fixture");
    auto listing = fs.read_dir("", visitor);
    TEST_ASSERT(!listing.ok() && listing.error() == Error::BadFs, "readdir detects a two-cluster cycle");
    FatDirEntry file{};
    TEST_ASSERT(fs.find_file("MISSING.TXT", &file) == Error::BadFs, "Lookup detects cycle");
    TEST_ASSERT(fs.mkdir("NEW") == Error::BadFs, "mkdir detects cycle");
    TEST_ASSERT(fs.create_file("NEW.TXT") == Error::BadFs, "create detects cycle");
    TEST_ASSERT(fs.unlink("MISSING.TXT") == Error::BadFs, "unlink detects cycle");
    TEST_ASSERT(fs.rmdir("MISSING") == Error::BadFs, "rmdir detects cycle");
    const uint32_t invalid_links[] = {0, 1, 6, fat::FAT32_RESERVED_MIN, fat::FAT32_BAD_CLUSTER};
    for (uint32_t invalid : invalid_links) {
        fs.unmount();
        image.set_link(2, invalid);
        TEST_ASSERT(fs.mount(&image) == Error::None, "Remounted invalid-link fixture");
        listing = fs.read_dir("", visitor);
        TEST_ASSERT(!listing.ok() && listing.error() == Error::BadFs, "Free/reserved/bad/out-of-volume link rejected");
    }
    fs.unmount();
    image.fail_fat_reads(true);
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted FAT I/O-error fixture");
    listing = fs.read_dir("", visitor);
    TEST_ASSERT(!listing.ok() && listing.error() == Error::Io, "FAT read failure remains an I/O error");
    fs.unmount();
    TEST_END();
}

void test_cyclic_file_removal() {
    TEST_START("Cyclic file cannot free live FAT links or loop during removal");
    FatImage image(1);
    FatInfo fs;
    if (!image.ready()) {
        TEST_ASSERT(false, "Allocated image");
        TEST_END();
        return;
    }
    image.set_link(3, 4);
    image.set_link(4, 3);
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted cyclic file fixture");
    TEST_ASSERT(fs.unlink("FILE.TXT") == Error::BadFs, "Removal validates the entire chain before freeing it");
    TEST_ASSERT(image.link(3) == 4 && image.link(4) == 3, "Rejected removal preserves both links");
    FatDirEntry file{};
    TEST_ASSERT(fs.find_file("FILE.TXT", &file) == Error::None, "Found cyclic file fixture");
    file.file_size = 6 * 512;
    auto* bytes = static_cast<uint8_t*>(kmalloc(file.file_size));
    if (bytes) {
        auto result = fs.read_file(&file, bytes, 0, file.file_size);
        TEST_ASSERT(!result.ok() && result.error() == Error::BadFs, "File read detects repeated clusters");
        kfree(bytes);
    }
    fs.unmount();
    TEST_END();
}

}  // namespace

namespace fat_validation_test {
void test() {
    tests_passed = tests_failed = 0;
    intr::Guard guard;
    test_geometry();
    test_large_cluster_io();
    test_directory_chains();
    test_cyclic_file_removal();
    TEST_SUMMARY("FAT validation");
}
}  // namespace fat_validation_test
