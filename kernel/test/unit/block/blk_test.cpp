#include "test/test_defs.h"
#include "block/blk.h"

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Get device by index (existing devices)
// ============================================================================

static void test_find_device_by_index() {
    TEST_START("BlockManager find_device by index");

    TEST_ASSERT(BlockManager::find_device(-1) == nullptr, "Negative index returns nullptr");

    int count = BlockManager::device_count();
    TEST_ASSERT(BlockManager::find_device(count + 10) == nullptr, "Out-of-range index returns nullptr");

    if (count > 0) {
        BlockDevice* dev = BlockManager::find_device(0);
        TEST_ASSERT(dev != nullptr, "Index 0 returns valid device");
        TEST_ASSERT(dev->name[0] != '\0', "Device has a name");
    }

    TEST_END();
}

// ============================================================================
// Get device by name
// ============================================================================

static void test_find_device_by_name() {
    TEST_START("BlockManager find_device by name");

    TEST_ASSERT(BlockManager::find_device(static_cast<const char*>(nullptr)) == nullptr,
                "nullptr name returns nullptr");
    TEST_ASSERT(BlockManager::find_device("nonexistent_device_xyz") == nullptr, "Non-existent name returns nullptr");

    int count = BlockManager::device_count();
    if (count > 0) {
        BlockDevice* dev = BlockManager::find_device(0);
        if (dev) {
            BlockDevice* found = BlockManager::find_device(dev->name);
            TEST_ASSERT(found == dev, "Lookup by name returns same device");
        }
    }

    TEST_END();
}

// ============================================================================
// Get device by type
// ============================================================================

static void test_find_device_by_type() {
    TEST_START("BlockManager find_device by type");

    BlockDevice* none_dev = BlockManager::find_device(blk::DeviceType::None);
    // DeviceType::None should not match any registered device
    TEST_ASSERT(none_dev == nullptr, "DeviceType::None returns nullptr");

    int count = BlockManager::device_count();
    if (count > 0) {
        BlockDevice* disk = BlockManager::find_device(blk::DeviceType::Disk);
        if (disk) {
            TEST_ASSERT(disk->type == blk::DeviceType::Disk, "Disk type lookup matches");
        }
    }

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace blk_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_find_device_by_index();
    test_find_device_by_name();
    test_find_device_by_type();

    TEST_SUMMARY("Block Manager");
}

}  // namespace blk_test
