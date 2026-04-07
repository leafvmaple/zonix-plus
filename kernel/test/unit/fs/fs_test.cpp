#include "test/test_defs.h"

#include "block/blk.h"
#include "fs/vfs.h"
#include "lib/memory.h"
#include "lib/result.h"
#include "lib/string.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

bool ensure_system_mounted() {
    if (vfs::is_mounted("/")) {
        return true;
    }

    int count = BlockManager::get_device_count();
    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }
        if (vfs::mount("/", dev, "fat") == Error::None) {
            return true;
        }
    }

    return false;
}

Error open_first_existing_file(vfs::File** out, const char** chosen_path) {
    if (!out || !chosen_path) {
        return Error::Invalid;
    }

    *out = nullptr;
    *chosen_path = nullptr;

    static const char* candidates[] = {
        "/KERNEL.SYS",
        "/KERNEL.ELF",
        "/EFI/ZONIX/KERNEL.ELF",
    };

    for (const char* path : candidates) {
        vfs::File* file = nullptr;
        if (vfs::open(path, &file) == Error::None && file) {
            *out = file;
            *chosen_path = path;
            return Error::None;
        }
    }

    return Error::NotFound;
}

// --- Helper: count directory entries via readdir ---

class EntryCounter : public vfs::DirVisitor {
public:
    int count{};
    char first_name[32]{};

    int visit(const vfs::DirEntry& entry) override {
        if (count == 0) {
            strncpy(first_name, entry.name, sizeof(first_name) - 1);
        }
        count++;
        return 0;
    }
};

class EntryFinder : public vfs::DirVisitor {
public:
    const char* target{};
    bool found{};
    vfs::NodeType type{vfs::NodeType::Unknown};
    uint32_t size{};

    explicit EntryFinder(const char* name) : target(name) {}

    int visit(const vfs::DirEntry& entry) override {
        if (strcmp(entry.name, target) == 0) {
            found = true;
            type = entry.type;
            size = entry.size;
        }
        return 0;
    }
};

// ============================================================
// Test 1: overwrite write/read roundtrip (existing test)
// ============================================================

void test_fat_write_overwrite_roundtrip() {
    TEST_START("FAT overwrite write/read roundtrip");

    bool mounted = ensure_system_mounted();
    TEST_ASSERT(mounted, "System FAT volume mounted");
    if (!mounted) {
        TEST_END();
        return;
    }

    vfs::File* file = nullptr;
    const char* file_path = nullptr;
    Error rc = open_first_existing_file(&file, &file_path);
    TEST_ASSERT(rc == Error::None && file != nullptr, "Found writable existing FAT file");
    if (rc != Error::None || !file) {
        TEST_END();
        return;
    }

    vfs::Stat st{};
    rc = file->stat(&st);
    TEST_ASSERT(rc == Error::None, "stat() succeeds");
    TEST_ASSERT(st.type == vfs::NodeType::File, "Target is regular file");
    TEST_ASSERT(st.size > 0, "Target file has non-zero size");

    if (rc != Error::None || st.type != vfs::NodeType::File || st.size == 0) {
        vfs::close(file);
        TEST_END();
        return;
    }

    uint32_t offset = st.size > 128 ? 64U : 0U;
    uint32_t span = 16U;
    if (offset >= st.size) {
        offset = 0;
    }
    if (span > st.size - offset) {
        span = st.size - offset;
    }

    TEST_ASSERT(span > 0, "Have writable test range inside file size");
    if (span == 0) {
        vfs::close(file);
        TEST_END();
        return;
    }

    uint8_t original[16]{};
    uint8_t patch[16]{};
    uint8_t verify[16]{};

    auto nread_r = vfs::read(file, original, span, offset);
    TEST_ASSERT(nread_r.ok() && nread_r.value() == static_cast<int>(span), "Read original bytes succeeds");
    if (!nread_r.ok() || nread_r.value() != static_cast<int>(span)) {
        vfs::close(file);
        TEST_END();
        return;
    }

    for (uint32_t i = 0; i < span; i++) {
        patch[i] = static_cast<uint8_t>(original[i] ^ 0xA5U);
    }

    auto nwritten_r = vfs::write(file, patch, span, offset);
    TEST_ASSERT(nwritten_r.ok() && nwritten_r.value() == static_cast<int>(span), "Overwrite write succeeds");

    auto nverify_r = vfs::read(file, verify, span, offset);
    TEST_ASSERT(nverify_r.ok() && nverify_r.value() == static_cast<int>(span), "Read-back after write succeeds");
    TEST_ASSERT(memcmp(verify, patch, span) == 0, "Read-back bytes match written bytes");

    auto nrestore_r = vfs::write(file, original, span, offset);
    TEST_ASSERT(nrestore_r.ok() && nrestore_r.value() == static_cast<int>(span), "Restore original bytes succeeds");

    auto nfinal_r = vfs::read(file, verify, span, offset);
    TEST_ASSERT(nfinal_r.ok() && nfinal_r.value() == static_cast<int>(span), "Final read succeeds");
    TEST_ASSERT(memcmp(verify, original, span) == 0, "Original bytes restored");

    cprintf("  (target file: %s, offset=%d, bytes=%d)\n", file_path, offset, span);

    vfs::close(file);
    TEST_END();
}

// ============================================================
// Test 2: stat on root directory and existing file
// ============================================================

void test_fat_stat() {
    TEST_START("FAT stat root and file");

    vfs::Stat st{};
    Error rc = vfs::stat("/", &st);
    TEST_ASSERT(rc == Error::None, "stat(\"/\") succeeds");
    TEST_ASSERT(st.type == vfs::NodeType::Directory, "Root is Directory type");

    vfs::File* file = nullptr;
    const char* file_path = nullptr;
    rc = open_first_existing_file(&file, &file_path);
    if (rc != Error::None || !file) {
        cprintf("  (no existing file found, skipping file stat)\n");
        TEST_END();
        return;
    }

    vfs::Stat fst{};
    rc = file->stat(&fst);
    TEST_ASSERT(rc == Error::None, "stat() on existing file succeeds");
    TEST_ASSERT(fst.type == vfs::NodeType::File, "File has File type");
    TEST_ASSERT(fst.size > 0, "File has non-zero size");

    cprintf("  (file: %s, size=%d)\n", file_path, fst.size);

    vfs::close(file);
    TEST_END();
}

// ============================================================
// Test 3: readdir on root directory
// ============================================================

void test_fat_readdir() {
    TEST_START("FAT readdir root");

    EntryCounter counter{};
    auto rc = vfs::readdir("/", counter);
    TEST_ASSERT(rc.ok(), "readdir(\"/\") succeeds");
    TEST_ASSERT(counter.count > 0, "Root directory has entries");

    cprintf("  (root has %d entries, first: %s)\n", counter.count, counter.first_name);

    TEST_END();
}

// ============================================================
// Test 4: mkdir + readdir + rmdir
// ============================================================

void test_fat_mkdir_rmdir() {
    TEST_START("FAT mkdir/rmdir lifecycle");

    Error rc = vfs::mkdir("/TESTDIR");
    TEST_ASSERT(rc == Error::None, "mkdir(\"/TESTDIR\") succeeds");
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Verify directory exists via stat.
    vfs::Stat st{};
    rc = vfs::stat("/TESTDIR", &st);
    TEST_ASSERT(rc == Error::None, "stat(\"/TESTDIR\") succeeds after mkdir");
    TEST_ASSERT(st.type == vfs::NodeType::Directory, "TESTDIR is Directory type");

    // Verify directory appears in root listing.
    EntryFinder finder("TESTDIR");
    vfs::readdir("/", finder);
    TEST_ASSERT(finder.found, "TESTDIR appears in root readdir");
    TEST_ASSERT(finder.type == vfs::NodeType::Directory, "TESTDIR listed as directory");

    // Verify readdir on new empty directory works.
    EntryCounter counter{};
    auto rd_rc = vfs::readdir("/TESTDIR", counter);
    TEST_ASSERT(rd_rc.ok(), "readdir(\"/TESTDIR\") succeeds");

    // Duplicate mkdir should fail.
    rc = vfs::mkdir("/TESTDIR");
    TEST_ASSERT(rc != Error::None, "Duplicate mkdir fails as expected");

    // Remove the directory.
    rc = vfs::rmdir("/TESTDIR");
    TEST_ASSERT(rc == Error::None, "rmdir(\"/TESTDIR\") succeeds");

    // Verify it no longer exists.
    rc = vfs::stat("/TESTDIR", &st);
    TEST_ASSERT(rc != Error::None, "stat(\"/TESTDIR\") fails after rmdir");

    TEST_END();
}

// ============================================================
// Test 5: create file + open + unlink
// ============================================================

void test_fat_create_unlink() {
    TEST_START("FAT create/unlink file lifecycle");

    Error rc = vfs::create("/TESTFILE.TXT");
    TEST_ASSERT(rc == Error::None, "create(\"/TESTFILE.TXT\") succeeds");
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Verify file exists via stat.
    vfs::Stat st{};
    rc = vfs::stat("/TESTFILE.TXT", &st);
    TEST_ASSERT(rc == Error::None, "stat new file succeeds");
    TEST_ASSERT(st.type == vfs::NodeType::File, "New file has File type");
    TEST_ASSERT(st.size == 0, "New file has zero size");

    // Open and close the new file.
    vfs::File* file = nullptr;
    rc = vfs::open("/TESTFILE.TXT", &file);
    TEST_ASSERT(rc == Error::None && file != nullptr, "open new file succeeds");
    if (file) {
        vfs::close(file);
    }

    // Verify file in root listing.
    EntryFinder finder("TESTFILE.TXT");
    vfs::readdir("/", finder);
    TEST_ASSERT(finder.found, "TESTFILE.TXT appears in root readdir");

    // Duplicate create should fail.
    rc = vfs::create("/TESTFILE.TXT");
    TEST_ASSERT(rc != Error::None, "Duplicate create fails as expected");

    // Unlink the file.
    rc = vfs::unlink("/TESTFILE.TXT");
    TEST_ASSERT(rc == Error::None, "unlink(\"/TESTFILE.TXT\") succeeds");

    // Verify it no longer exists.
    rc = vfs::stat("/TESTFILE.TXT", &st);
    TEST_ASSERT(rc != Error::None, "stat fails after unlink");

    TEST_END();
}

// ============================================================
// Test 6: nested mkdir - create file in subdirectory
// ============================================================

void test_fat_nested_mkdir_create() {
    TEST_START("FAT nested mkdir and create file");

    // Create parent directory.
    Error rc = vfs::mkdir("/SUBTEST");
    TEST_ASSERT(rc == Error::None, "mkdir(\"/SUBTEST\") succeeds");
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Create a file inside the subdirectory.
    rc = vfs::create("/SUBTEST/INNER.TXT");
    TEST_ASSERT(rc == Error::None, "create(\"/SUBTEST/INNER.TXT\") succeeds");

    // Stat the nested file.
    vfs::Stat st{};
    rc = vfs::stat("/SUBTEST/INNER.TXT", &st);
    TEST_ASSERT(rc == Error::None, "stat nested file succeeds");
    TEST_ASSERT(st.type == vfs::NodeType::File, "Nested file has File type");

    // Readdir on subdirectory should find the file.
    EntryFinder finder("INNER.TXT");
    vfs::readdir("/SUBTEST", finder);
    TEST_ASSERT(finder.found, "INNER.TXT appears in SUBTEST readdir");

    // rmdir on non-empty directory should fail.
    rc = vfs::rmdir("/SUBTEST");
    TEST_ASSERT(rc != Error::None, "rmdir non-empty dir fails as expected");

    // Clean up: unlink the file, then rmdir.
    rc = vfs::unlink("/SUBTEST/INNER.TXT");
    TEST_ASSERT(rc == Error::None, "unlink nested file succeeds");

    rc = vfs::rmdir("/SUBTEST");
    TEST_ASSERT(rc == Error::None, "rmdir empty SUBTEST succeeds");

    // Verify parent directory is gone.
    rc = vfs::stat("/SUBTEST", &st);
    TEST_ASSERT(rc != Error::None, "stat SUBTEST fails after rmdir");

    TEST_END();
}

// ============================================================
// Test 7: open/stat on non-existent paths
// ============================================================

void test_fat_nonexistent_paths() {
    TEST_START("FAT non-existent path errors");

    vfs::File* file = nullptr;
    Error rc = vfs::open("/NOSUCHFILE.BIN", &file);
    TEST_ASSERT(rc != Error::None, "open non-existent file fails");
    TEST_ASSERT(file == nullptr, "out_file stays null on failure");

    vfs::Stat st{};
    rc = vfs::stat("/NOSUCHFILE.BIN", &st);
    TEST_ASSERT(rc != Error::None, "stat non-existent file fails");

    rc = vfs::stat("/NODIR/FILE.TXT", &st);
    TEST_ASSERT(rc != Error::None, "stat with non-existent parent fails");

    rc = vfs::unlink("/NOSUCHFILE.BIN");
    TEST_ASSERT(rc != Error::None, "unlink non-existent file fails");

    rc = vfs::rmdir("/NOSUCHDIR");
    TEST_ASSERT(rc != Error::None, "rmdir non-existent dir fails");

    TEST_END();
}

// ============================================================
// Test 8: unlink on directory should fail (use rmdir instead)
// ============================================================

void test_fat_unlink_dir_fails() {
    TEST_START("FAT unlink directory fails");

    Error rc = vfs::mkdir("/UNLNKDIR");
    TEST_ASSERT(rc == Error::None, "mkdir(\"/UNLNKDIR\") succeeds");
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // unlink on a directory should fail.
    rc = vfs::unlink("/UNLNKDIR");
    TEST_ASSERT(rc != Error::None, "unlink on directory fails as expected");

    // Clean up.
    rc = vfs::rmdir("/UNLNKDIR");
    TEST_ASSERT(rc == Error::None, "rmdir cleanup succeeds");

    TEST_END();
}

// ============================================================
// Test 9: readdir on subdirectory with . and .. entries
// ============================================================

void test_fat_readdir_subdir() {
    TEST_START("FAT readdir subdirectory");

    Error rc = vfs::mkdir("/RDTEST");
    TEST_ASSERT(rc == Error::None, "mkdir(\"/RDTEST\") succeeds");
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Create two files in the subdirectory.
    rc = vfs::create("/RDTEST/FILE1.TXT");
    TEST_ASSERT(rc == Error::None, "create FILE1.TXT succeeds");

    rc = vfs::create("/RDTEST/FILE2.TXT");
    TEST_ASSERT(rc == Error::None, "create FILE2.TXT succeeds");

    // Readdir should list both files (plus . and ..).
    EntryCounter counter{};
    auto rd_rc = vfs::readdir("/RDTEST", counter);
    TEST_ASSERT(rd_rc.ok(), "readdir(\"/RDTEST\") succeeds");
    TEST_ASSERT(counter.count >= 2, "Subdirectory has at least 2 entries");

    cprintf("  (RDTEST has %d entries)\n", counter.count);

    // Clean up.
    vfs::unlink("/RDTEST/FILE1.TXT");
    vfs::unlink("/RDTEST/FILE2.TXT");
    vfs::rmdir("/RDTEST");

    TEST_END();
}

}  // namespace

namespace fs_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_fat_write_overwrite_roundtrip();
    test_fat_stat();
    test_fat_readdir();
    test_fat_mkdir_rmdir();
    test_fat_create_unlink();
    test_fat_nested_mkdir_create();
    test_fat_nonexistent_paths();
    test_fat_unlink_dir_fails();
    test_fat_readdir_subdir();

    TEST_SUMMARY("File System");
}

}  // namespace fs_test
