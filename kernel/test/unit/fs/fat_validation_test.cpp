#include "test/test_defs.h"
#include "fs/fat.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "drivers/intr.h"
#include "test/unit/fs/fat_image.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

using fat_test::FatImage;

class IgnoreEntries : public FatInfo::DirVisitor {
public:
    int visit(const FatInfo::DirectoryRecord&) override { return 0; }
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
    assert(fs.unmount() == Error::None);
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
    FatInfo::EntryLocation location{};
    TEST_ASSERT(fs.find_file("FILE.TXT", &file, &location) == Error::None, "Found file");
    auto* bytes = static_cast<uint8_t*>(kmalloc(image.cluster_bytes()));
    if (bytes) {
        auto result = fs.read_file(&file, bytes, 0, file.file_size);
        bool intact = result.ok() && result.value() == static_cast<int>(image.cluster_bytes());
        for (size_t i = 0; i < image.cluster_bytes(); ++i) {
            intact = intact && bytes[i] == static_cast<uint8_t>(i);
        }
        TEST_ASSERT(intact, "Full cluster read preserves every byte");
        memset(bytes, 0xA5, image.cluster_bytes());
        auto written = fs.write_file(location, bytes, 0, file.file_size);
        TEST_ASSERT(written.ok() && written.value() == static_cast<int>(image.cluster_bytes()) &&
                        image.cluster_data(3)[image.cluster_bytes() - 1] == 0xA5,
                    "Full cluster write reaches its last byte");
        auto eof = fs.read_file(&file, bytes, file.file_size, 1);
        TEST_ASSERT(eof.ok() && eof.value() == 0, "Read at EOF returns zero");
        kfree(bytes);
    } else {
        TEST_ASSERT(false, "Allocated I/O buffer");
    }
    assert(fs.unmount() == Error::None);
    TEST_END();
}

// Every device write of a removable card waits for the card to program it,
// so a write sends each run of whole sectors in a cluster at once and
// publishes the grown size once at the end.
void test_write_batching() {
    TEST_START("FAT writes whole sectors together and the directory entry once");
    FatImage image;
    FatInfo fs;
    auto* bytes = static_cast<uint8_t*>(kmalloc(image.cluster_bytes()));
    if (!image.ready() || !bytes) {
        TEST_ASSERT(false, "Allocated image and buffer");
        kfree(bytes);
        TEST_END();
        return;
    }
    reinterpret_cast<FatDirEntry*>(image.cluster_data(2))->file_size = BlockDevice::BLOCK_SIZE_BYTES;
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted single-cluster fixture");
    FatDirEntry file{};
    FatInfo::EntryLocation location{};
    TEST_ASSERT(fs.find_file("FILE.TXT", &file, &location) == Error::None, "Found file");
    for (size_t i = 0; i < image.cluster_bytes(); ++i) {
        bytes[i] = static_cast<uint8_t>(i * 7);
    }
    const size_t before = image.write_calls();
    image.watch_writes(image.cluster_lba(2));
    auto written = fs.write_file(location, bytes, 0, image.cluster_bytes());
    TEST_ASSERT(written.ok() && written.value() == static_cast<int>(image.cluster_bytes()) &&
                    memcmp(image.cluster_data(3), bytes, image.cluster_bytes()) == 0,
                "Growing a file across its whole cluster stores every byte");
    TEST_ASSERT(image.write_calls() - before == 2 && image.watched_writes() == 1,
                "One write carries the cluster's sectors and one publishes the new size");
    TEST_ASSERT(fs.find_file("FILE.TXT", &file, &location) == Error::None && file.file_size == image.cluster_bytes(),
                "Directory entry records the grown size");
    // Partial sectors around a whole one keep their neighbours.
    auto patch = fs.write_file(location, bytes + 1, 100, 1000);
    bool intact = patch.ok() && patch.value() == 1000;
    for (size_t i = 0; i < image.cluster_bytes(); ++i) {
        const uint8_t expected = i >= 100 && i < 1100 ? bytes[i - 99] : bytes[i];
        intact = intact && image.cluster_data(3)[i] == expected;
    }
    TEST_ASSERT(intact, "Unaligned write changes exactly its range");
    kfree(bytes);
    assert(fs.unmount() == Error::None);
    TEST_END();
}

void test_offset_reads_skip_clusters() {
    TEST_START("FAT reads at an offset read only the clusters they return");
    FatImage image(1, 4);
    FatInfo fs;
    if (!image.ready()) {
        TEST_ASSERT(false, "Allocated image");
        TEST_END();
        return;
    }
    // FILE.TXT spans clusters 3, 4 and 5; each byte names its cluster.
    image.set_link(3, 4);
    image.set_link(4, 5);
    reinterpret_cast<FatDirEntry*>(image.cluster_data(2))->file_size = 3 * image.cluster_bytes();
    for (uint32_t cluster = 3; cluster <= 5; ++cluster) {
        memset(image.cluster_data(cluster), static_cast<int>(cluster), image.cluster_bytes());
    }
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted three-cluster fixture");
    FatDirEntry file{};
    FatInfo::EntryLocation location{};
    TEST_ASSERT(fs.find_file("FILE.TXT", &file, &location) == Error::None, "Found file");
    uint8_t byte{};
    size_t before = image.data_sectors_read();
    auto last = fs.read_file(&file, &byte, 2 * image.cluster_bytes() + 5, 1);
    TEST_ASSERT(last.ok() && last.value() == 1 && byte == 5, "Read in the third cluster returns its data");
    TEST_ASSERT(image.data_sectors_read() - before == 1, "Only the cluster holding the offset is read");
    uint8_t span[2]{};
    before = image.data_sectors_read();
    auto across = fs.read_file(&file, span, image.cluster_bytes() - 1, 2);
    TEST_ASSERT(across.ok() && across.value() == 2 && span[0] == 3 && span[1] == 4 &&
                    image.data_sectors_read() - before == 2,
                "A read across a cluster boundary reads exactly the two clusters");
    assert(fs.unmount() == Error::None);
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
        assert(fs.unmount() == Error::None);
        image.set_link(2, invalid);
        TEST_ASSERT(fs.mount(&image) == Error::None, "Remounted invalid-link fixture");
        listing = fs.read_dir("", visitor);
        TEST_ASSERT(!listing.ok() && listing.error() == Error::BadFs, "Free/reserved/bad/out-of-volume link rejected");
    }
    assert(fs.unmount() == Error::None);
    image.fail_fat_reads(true);
    TEST_ASSERT(fs.mount(&image) == Error::None, "Mounted FAT I/O-error fixture");
    listing = fs.read_dir("", visitor);
    TEST_ASSERT(!listing.ok() && listing.error() == Error::Io, "FAT read failure remains an I/O error");
    assert(fs.unmount() == Error::None);
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
    assert(fs.unmount() == Error::None);
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
    auto written = fs.write_file({2, 0}, &byte, 0, 1);
    TEST_ASSERT(!written.ok() && written.error() == Error::Busy, "File write preserves busy");
    memcpy(image.cluster_data(5), &file, sizeof(file));
    TEST_ASSERT(fs.rmdir("SUB") == Error::NotEmpty, "Nonempty directory is an ordinary refusal");
    assert(fs.unmount() == Error::None);
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
        assert(fs.unmount() == Error::None);
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
        assert(fs.unmount() == Error::None);
    }
    TEST_END();
}

// Windows writes "bin" as the 8.3 name "BIN" with a lowercase flag and no
// long name; the VisionFive 2 root file system was copied that way.
void test_nt_lowercase_names() {
    TEST_START("FAT shows the case Windows records for 8.3 names");
    FatDirEntry entry{};
    memcpy(entry.name, "KERNEL  ", sizeof(entry.name));
    memcpy(entry.ext, "LOG", sizeof(entry.ext));
    char shown[13]{};
    entry.nt_reserved = FAT_NT_LOWER_BASE | FAT_NT_LOWER_EXT;
    entry.get_display_name(shown, sizeof(shown));
    TEST_ASSERT(strcmp(shown, "kernel.log") == 0, "Both lowercase flags lower the whole name");
    entry.nt_reserved = FAT_NT_LOWER_EXT;
    entry.get_display_name(shown, sizeof(shown));
    TEST_ASSERT(strcmp(shown, "KERNEL.log") == 0, "Each flag covers only its own part");
    entry.get_filename(shown, sizeof(shown));
    TEST_ASSERT(strcmp(shown, "KERNEL.LOG") == 0, "Lookups keep the stored upper-case form");

    // Names Zonix creates keep their case too: logd makes /var/log/kernel.log.
    class Names : public FatInfo::DirVisitor {
    public:
        int visit(const FatInfo::DirectoryRecord& record) override {
            char name[13]{};
            record.entry.get_display_name(name, sizeof(name));
            if (strcmp(name, "kernel.log") == 0 && !record.long_name)
                lower_short = true;
            if (record.long_name && strcmp(record.long_name, "Mixed.txt") == 0)
                mixed_long = true;
            return 0;
        }
        bool lower_short{};
        bool mixed_long{};
    };
    FatImage image;
    FatInfo fs;
    if (image.ready() && fs.mount(&image) == Error::None) {
        TEST_ASSERT(fs.create_file("kernel.log") == Error::None && fs.create_file("Mixed.txt") == Error::None,
                    "Created a lowercase and a mixed-case file");
        Names names;
        TEST_ASSERT(fs.read_dir("", names).ok(), "Listed the root directory");
        TEST_ASSERT(names.lower_short, "A lowercase 8.3 name is stored short and shown lowercase");
        TEST_ASSERT(names.mixed_long, "A mixed-case name keeps its case in a long name");
        assert(fs.unmount() == Error::None);
    } else {
        TEST_ASSERT(false, "Mounted name-case fixture");
    }
    TEST_END();
}

// Directories an older Zonix created as "VAR" are fixed by renaming them to
// "var". Lookups ignore case, so the rename must still rewrite the case flag.
void test_case_only_rename() {
    TEST_START("FAT renames a name to the same letters in another case");
    class Shown : public FatInfo::DirVisitor {
    public:
        int visit(const FatInfo::DirectoryRecord& record) override {
            char name[13]{};
            record.entry.get_display_name(name, sizeof(name));
            bool var = strcmp(name, "var") == 0 || strcmp(name, "VAR") == 0;
            if (var && !record.long_name && record.entry.is_directory()) {
                memcpy(shown, name, sizeof(name));
                ++count;
            }
            return 0;
        }
        char shown[13]{};
        int count{};
    };
    FatImage image;
    FatInfo fs;
    if (image.ready())
        image.set_link(4, fat::FAT32_FREE);  // Room for the directory.
    if (image.ready() && fs.mount(&image) == Error::None) {
        bool made = fs.mkdir("VAR") == Error::None && fs.create_file("VAR/kernel.log") == Error::None;
        bool lowered = fs.rename("VAR", "var", false) == Error::None;
        Shown lower;
        bool listed = fs.read_dir("", lower).ok();
        FatDirEntry inside{};
        bool kept = fs.find_file("var/kernel.log", &inside, nullptr) == Error::None;
        bool raised = fs.rename("var", "VAR", false) == Error::None;
        Shown upper;
        listed = listed && fs.read_dir("", upper).ok();
        TEST_ASSERT(made && lowered && raised && listed, "Created, renamed and listed the directory");
        TEST_ASSERT(lower.count == 1 && strcmp(lower.shown, "var") == 0 && kept,
                    "Renaming VAR to var shows it lowercase in one entry that keeps its contents");
        TEST_ASSERT(upper.count == 1 && strcmp(upper.shown, "VAR") == 0, "Renaming it back shows it uppercase");
        assert(fs.unmount() == Error::None);
    } else {
        TEST_ASSERT(false, "Mounted case-rename fixture");
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
    test_write_batching();
    test_offset_reads_skip_clusters();
    test_directory_chains();
    test_cyclic_file_removal();
    test_device_errors();
    test_mkdir_rollback();
    test_directory_extension_rollback();
    test_nt_lowercase_names();
    test_case_only_rename();
    TEST_SUMMARY("FAT validation");
}
}  // namespace fat_validation_test
