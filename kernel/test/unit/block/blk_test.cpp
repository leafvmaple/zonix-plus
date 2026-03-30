#include "test/test_defs.h"
#include "block/blk.h"
#include "lib/string.h"
#include "lib/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Mock block device for testing
// ============================================================================

class MockBlockDevice : public BlockDevice {
public:
    uint8_t backing[512]{};
    int read_count{};
    int write_count{};

    MockBlockDevice(const char* dev_name, blk::DeviceType dev_type, uint32_t dev_size) {
        strncpy(name, dev_name, sizeof(name) - 1);
        type = dev_type;
        size = dev_size;
    }

    int read(uint32_t, void* buf, size_t) override {
        read_count++;
        memcpy(buf, backing, 512);
        return 0;
    }

    int write(uint32_t, const void* buf, size_t) override {
        write_count++;
        memcpy(backing, buf, 512);
        return 0;
    }
};

// ============================================================================
// Device count (observe-only, do not register real devices)
// ============================================================================

static void test_device_count() {
    TEST_START("BlockManager device count");

    int count = BlockManager::get_device_count();
    TEST_ASSERT(count >= 0, "Device count is non-negative");
    cprintf("  (Current device count: %d)\n", count);

    TEST_END();
}

// ============================================================================
// Mock device read/write
// ============================================================================

static void test_mock_readwrite() {
    TEST_START("Mock BlockDevice read/write");

    MockBlockDevice mock("test0", blk::DeviceType::Disk, 1024);

    uint8_t wbuf[512], rbuf[512];
    for (int i = 0; i < 512; i++)
        wbuf[i] = static_cast<uint8_t>(i & 0xFF);

    int rc = mock.write(0, wbuf, 1);
    TEST_ASSERT(rc == 0, "Mock write returns 0");
    TEST_ASSERT(mock.write_count == 1, "Write count incremented");

    rc = mock.read(0, rbuf, 1);
    TEST_ASSERT(rc == 0, "Mock read returns 0");
    TEST_ASSERT(mock.read_count == 1, "Read count incremented");
    TEST_ASSERT(memcmp(wbuf, rbuf, 512) == 0, "Read data matches written data");

    TEST_END();
}

// ============================================================================
// Get device by index (existing devices)
// ============================================================================

static void test_get_device_by_index() {
    TEST_START("BlockManager get_device by index");

    TEST_ASSERT(BlockManager::get_device(-1) == nullptr, "Negative index returns nullptr");

    int count = BlockManager::get_device_count();
    TEST_ASSERT(BlockManager::get_device(count + 10) == nullptr, "Out-of-range index returns nullptr");

    if (count > 0) {
        BlockDevice* dev = BlockManager::get_device(0);
        TEST_ASSERT(dev != nullptr, "Index 0 returns valid device");
        TEST_ASSERT(dev->name[0] != '\0', "Device has a name");
    }

    TEST_END();
}

// ============================================================================
// Get device by name
// ============================================================================

static void test_get_device_by_name() {
    TEST_START("BlockManager get_device by name");

    TEST_ASSERT(BlockManager::get_device(static_cast<const char*>(nullptr)) == nullptr, "nullptr name returns nullptr");
    TEST_ASSERT(BlockManager::get_device("nonexistent_device_xyz") == nullptr, "Non-existent name returns nullptr");

    int count = BlockManager::get_device_count();
    if (count > 0) {
        BlockDevice* dev = BlockManager::get_device(0);
        if (dev) {
            BlockDevice* found = BlockManager::get_device(dev->name);
            TEST_ASSERT(found == dev, "Lookup by name returns same device");
        }
    }

    TEST_END();
}

// ============================================================================
// Get device by type
// ============================================================================

static void test_get_device_by_type() {
    TEST_START("BlockManager get_device by type");

    BlockDevice* none_dev = BlockManager::get_device(blk::DeviceType::None);
    // DeviceType::None should not match any registered device
    TEST_ASSERT(none_dev == nullptr, "DeviceType::None returns nullptr");

    int count = BlockManager::get_device_count();
    if (count > 0) {
        BlockDevice* disk = BlockManager::get_device(blk::DeviceType::Disk);
        if (disk) {
            TEST_ASSERT(disk->type == blk::DeviceType::Disk, "Disk type lookup matches");
        }
    }

    TEST_END();
}

// ============================================================================
// Block device constants
// ============================================================================

static void test_block_constants() {
    TEST_START("BlockDevice constants");

    TEST_ASSERT(BlockDevice::SIZE == 512, "Sector size is 512 bytes");
    TEST_ASSERT(BlockManager::MAX_DEV >= 4, "MAX_DEV is at least 4");

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace blk_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_device_count();
    test_mock_readwrite();
    test_get_device_by_index();
    test_get_device_by_name();
    test_get_device_by_type();
    test_block_constants();

    TEST_SUMMARY("Block Manager");
}

}  // namespace blk_test
