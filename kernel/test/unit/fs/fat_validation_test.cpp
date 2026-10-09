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
    void fail_read_at(uint32_t block, Error error) {
        read_block_ = block;
        read_error_ = error;
    }
    void fail_write_at(uint32_t block, size_t matching_write, Error error, size_t slot = 0, bool partial = false) {
        assert(slot < 2 && matching_write > 0);
        write_failures_[slot] = {block, matching_write, error, partial};
    }
    Error read(uint32_t block, void* buffer, size_t count) override {
        if (block > block_count || count > block_count - block) {
            return Error::Io;
        }
        if (fail_fat_reads_ && block == 1) {
            return Error::Io;
        }
        if (read_error_ != Error::None && block == read_block_) {
            return read_error_;
        }
        memcpy(buffer, data_ + block * BLOCK_SIZE_BYTES, count * BLOCK_SIZE_BYTES);
        return Error::None;
    }
    Error write(uint32_t block, const void* buffer, size_t count) override {
        if (block > block_count || count > block_count - block) {
            return Error::Io;
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
    uint32_t read_block_{};
    Error read_error_{Error::None};
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

void test_device_errors() {
    TEST_START("FAT propagates device errors from root, subdirectory and file I/O");
    FatImage image(1);
    FatInfo fs;
    IgnoreEntries visitor;
    if (!image.ready()) {
        TEST_ASSERT(false, "Allocated image");
        TEST_END();
        return;
    }
    auto* entries = reinterpret_cast<FatDirEntry*>(image.cluster_data(2));
    entries[1] = entries[0];
    memcpy(entries[1].name, "SUB     ", 8);
    memcpy(entries[1].ext, "   ", 3);
    entries[1].attr = FAT_ATTR_DIRECTORY;
    entries[1].first_cluster_low = 5;
    entries[1].file_size = 0;
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted device-error fixture");
    image.fail_read_at(2, Error::Timeout);
    auto listing = fs.read_dir("", visitor);
    TEST_ASSERT(!listing.ok() && listing.error() == Error::Timeout, "Root listing preserves timeout");
    image.fail_read_at(5, Error::NoDevice);
    listing = fs.read_dir("SUB", visitor);
    TEST_ASSERT(!listing.ok() && listing.error() == Error::NoDevice, "Subdirectory listing preserves device loss");
    image.fail_read_at(3, Error::Timeout);
    FatDirEntry file = entries[0];
    uint8_t byte{};
    auto read = fs.read_file(&file, &byte, 0, 1);
    TEST_ASSERT(!read.ok() && read.error() == Error::Timeout, "File read preserves timeout");
    image.fail_read_at(0, Error::None);
    image.fail_write_at(3, 1, Error::Busy);
    auto written = fs.write_file(&file, &byte, 0, 1);
    TEST_ASSERT(!written.ok() && written.error() == Error::Busy, "File write preserves busy");
    memcpy(image.cluster_data(5), &file, sizeof(file));
    TEST_ASSERT(fs.rmdir("SUB") == Error::NotEmpty, "Nonempty directory is an ordinary refusal");
    fs.unmount();
    TEST_END();
}

void test_mkdir_rollback() {
    TEST_START("FAT mkdir preserves primary errors and owns rollback resources");
    for (int phase = 0; phase < 5; ++phase) {
        FatImage image(1);
        FatInfo fs;
        if (!image.ready()) {
            TEST_ASSERT(false, "Allocated image");
            break;
        }
        image.set_link(4, fat::FAT32_FREE);
        TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted mkdir failure fixture");
        Error expected = Error::Timeout;
        if (phase == 0) {
            expected = Error::NoDevice;
            image.fail_write_at(1, 1, expected);
        } else if (phase == 1) {
            image.fail_write_at(4, 1, expected);
        } else if (phase == 2) {
            expected = Error::Busy;
            image.fail_write_at(4, 2, expected);
        } else if (phase == 3) {
            expected = Error::NoDevice;
            image.fail_write_at(2, 1, expected, 0, true);
        } else {
            image.fail_write_at(4, 2, expected);
            image.fail_write_at(1, 2, Error::Busy, 1);
        }
        TEST_ASSERT(fs.mkdir("NEW") == expected, "Original allocation/write error survives cleanup");
        if (phase < 3) {
            TEST_ASSERT(image.link(4) == fat::FAT32_FREE, "Unpublished cluster is reclaimed");
        } else {
            TEST_ASSERT(image.link(4) == fat::FAT32_EOC_MAX, "Uncertain publication/failed cleanup retains allocation");
        }
        if (phase == 3) {
            FatDirEntry entry{};
            TEST_ASSERT(fs.find_file("NEW", &entry) == Error::None && entry.cluster() == 4,
                        "Partially published directory still references an allocated cluster");
        }
        fs.unmount();
    }
    TEST_END();
}

void test_directory_extension_rollback() {
    TEST_START("FAT initializes directory extensions before publishing their links");
    for (int phase = 0; phase < 4; ++phase) {
        FatImage image(1);
        FatInfo fs;
        if (!image.ready()) {
            TEST_ASSERT(false, "Allocated image");
            break;
        }
        auto* entries = reinterpret_cast<FatDirEntry*>(image.cluster_data(2));
        FatDirEntry file = entries[0];
        for (size_t i = 0; i < image.cluster_bytes() / sizeof(FatDirEntry); ++i) {
            entries[i] = file;
        }
        image.set_link(4, phase == 3 ? fat::FAT32_EOC_MAX : fat::FAT32_FREE);
        if (phase == 2) {
            image.set_link(5, fat::FAT32_FREE);
        }
        TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted full-directory fixture");
        Error expected = Error::Timeout;
        if (phase == 0) {
            image.fail_write_at(4, 2, expected);
        } else if (phase == 1) {
            expected = Error::NoDevice;
            image.fail_write_at(1, 2, expected);
        } else if (phase == 2) {
            image.fail_write_at(1, 3, expected);
            image.fail_write_at(1, 3, Error::Busy, 1);
        } else {
            expected = Error::Full;
        }
        Error error = phase == 2 ? fs.mkdir("NEW") : fs.create_file("NEW.TXT");
        TEST_ASSERT(error == expected, "Extension failure retains its precise primary error");
        TEST_ASSERT(image.link(2) == fat::FAT32_EOC_MAX, "Failed extension does not publish a freed tail");
        if (phase < 2) {
            TEST_ASSERT(image.link(4) == fat::FAT32_FREE, "Safely detached extension is reclaimed");
        } else if (phase == 2) {
            TEST_ASSERT(image.link(4) == fat::FAT32_EOC_MAX && image.link(5) == fat::FAT32_EOC_MAX,
                        "Failed tail restoration retains both extension and referenced child");
        }
        fs.unmount();
    }
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
    test_device_errors();
    test_mkdir_rollback();
    test_directory_extension_rollback();
    TEST_SUMMARY("FAT validation");
}
}  // namespace fat_validation_test
