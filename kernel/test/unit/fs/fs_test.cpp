#include "test/test_defs.h"

#include "block/blk.h"
#include "fs/vfs.h"
#include "lib/memory.h"

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
        if (vfs::mount("/", dev, "fat") == 0) {
            return true;
        }
    }

    return false;
}

int open_first_existing_file(vfs::File** out, const char** chosen_path) {
    if (!out || !chosen_path) {
        return -1;
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
        if (vfs::open(path, &file) == 0 && file) {
            *out = file;
            *chosen_path = path;
            return 0;
        }
    }

    return -1;
}

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
    int rc = open_first_existing_file(&file, &file_path);
    TEST_ASSERT(rc == 0 && file != nullptr, "Found writable existing FAT file");
    if (rc != 0 || !file) {
        TEST_END();
        return;
    }

    vfs::Stat st{};
    rc = file->stat(&st);
    TEST_ASSERT(rc == 0, "stat() succeeds");
    TEST_ASSERT(st.type == vfs::NodeType::File, "Target is regular file");
    TEST_ASSERT(st.size > 0, "Target file has non-zero size");

    if (rc != 0 || st.type != vfs::NodeType::File || st.size == 0) {
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

    int nread = vfs::read(file, original, span, offset);
    TEST_ASSERT(nread == static_cast<int>(span), "Read original bytes succeeds");
    if (nread != static_cast<int>(span)) {
        vfs::close(file);
        TEST_END();
        return;
    }

    for (uint32_t i = 0; i < span; i++) {
        patch[i] = static_cast<uint8_t>(original[i] ^ 0xA5U);
    }

    int nwritten = vfs::write(file, patch, span, offset);
    TEST_ASSERT(nwritten == static_cast<int>(span), "Overwrite write succeeds");

    int nverify = vfs::read(file, verify, span, offset);
    TEST_ASSERT(nverify == static_cast<int>(span), "Read-back after write succeeds");
    TEST_ASSERT(memcmp(verify, patch, span) == 0, "Read-back bytes match written bytes");

    int nrestore = vfs::write(file, original, span, offset);
    TEST_ASSERT(nrestore == static_cast<int>(span), "Restore original bytes succeeds");

    int nfinal = vfs::read(file, verify, span, offset);
    TEST_ASSERT(nfinal == static_cast<int>(span), "Final read succeeds");
    TEST_ASSERT(memcmp(verify, original, span) == 0, "Original bytes restored");

    cprintf("  (target file: %s, offset=%d, bytes=%d)\n", file_path, offset, span);

    vfs::close(file);
    TEST_END();
}

}  // namespace

namespace fs_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_fat_write_overwrite_roundtrip();

    TEST_SUMMARY("File System");
}

}  // namespace fs_test
