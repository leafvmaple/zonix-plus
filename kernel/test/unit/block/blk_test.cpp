#include "test/test_defs.h"
#include "block/blk.h"
#include "block/partition.h"
#include "lib/memory.h"
#include "lib/string.h"
#include <base/gpt.h>
#include <base/mbr.h>

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

// ============================================================================
// GPT data partitions
// ============================================================================

namespace {

// A disk in memory whose every sector starts with its own LBA.
class MemoryDisk final : public BlockDevice {
public:
    static constexpr uint32_t SECTORS = 64;

    MemoryDisk() {
        type = blk::DeviceType::Disk;
        block_count = SECTORS;
        name[0] = 'm';
        name[1] = 'd';
        for (uint32_t lba = 0; lba < SECTORS; ++lba)
            memcpy(sectors_[lba], &lba, sizeof(lba));
    }
    uint8_t* sector(uint32_t lba) { return sectors_[lba]; }

private:
    Error read_blocks(uint32_t lba, void* buf, size_t count) override {
        memcpy(buf, sectors_[lba], count * BLOCK_SIZE_BYTES);
        return Error::None;
    }
    Error write_blocks(uint32_t lba, const void* buf, size_t count) override {
        memcpy(sectors_[lba], buf, count * BLOCK_SIZE_BYTES);
        return Error::None;
    }
    uint8_t sectors_[SECTORS][BLOCK_SIZE_BYTES]{};
};

void add_entry(GptPartitionEntry* entries, int index, const GptGuid& type, uint64_t first, uint64_t last) {
    entries[index].type_guid = type;
    entries[index].starting_lba = first;
    entries[index].ending_lba = last;
}

}  // namespace

// The VisionFive 2 keeps the Zonix root file system on one GPT partition of a
// card that also holds firmware, an ESP and Linux.
static void test_gpt_data_partitions() {
    TEST_START("GPT basic-data partitions become block devices");
    auto* disk = new (sys::nothrow) MemoryDisk();
    TEST_ASSERT(disk != nullptr, "Allocated an in-memory disk");
    if (!disk) {
        TEST_END();
        return;
    }
    blk::PartitionRange ranges[4]{};

    // A bare file system also ends sector 0 with 0xAA55.
    disk->sector(0)[510] = 0x55;
    disk->sector(0)[511] = 0xAA;
    auto none = blk::find_data_partitions(disk, ranges, 4);
    TEST_ASSERT(none.ok() && none.value() == 0, "A disk without a GPT has no partitions");

    auto* mbr = reinterpret_cast<MbrHeader*>(disk->sector(0));
    mbr->partitions[0].type = GPT_PROTECTIVE_MBR_TYPE;
    auto* header = reinterpret_cast<GptHeader*>(disk->sector(1));
    memset(header, 0, sizeof(*header));
    header->signature = GPT_HEADER_SIGNATURE;
    header->partition_entry_lba = 2;
    header->num_partition_entries = 8;
    header->partition_entry_size = sizeof(GptPartitionEntry);
    auto* entries = reinterpret_cast<GptPartitionEntry*>(disk->sector(2));
    memset(entries, 0, BlockDevice::BLOCK_SIZE_BYTES);
    add_entry(entries, 0, ESP_GUID, 34, 39);
    add_entry(entries, 2, BASIC_DATA_GUID, 40, 63);
    add_entry(entries, 3, BASIC_DATA_GUID, 50, 64);  // Ends past the disk.
    auto found = blk::find_data_partitions(disk, ranges, 4);
    TEST_ASSERT(found.ok() && found.value() == 1, "Only the basic-data partition inside the disk is found");
    TEST_ASSERT(ranges[0].number == 3 && ranges[0].first_lba == 40 && ranges[0].block_count == 24,
                "The partition keeps its GPT number and extent");

    auto* partition = new (sys::nothrow) PartitionDevice();
    if (partition && found.ok() && found.value() == 1) {
        TEST_ASSERT(partition->attach(disk, ranges[0]) == Error::None && strcmp(partition->name, "mdp3") == 0,
                    "The partition is named after its disk and number");
        uint8_t block[BlockDevice::BLOCK_SIZE_BYTES]{};
        uint32_t lba{};
        TEST_ASSERT(partition->read(1, block, 1) == Error::None && (memcpy(&lba, block, sizeof(lba)), lba == 41),
                    "Partition block 1 is disk block 41");
        TEST_ASSERT(partition->read(24, block, 1) == Error::Invalid && partition->write(23, block, 2) == Error::Invalid,
                    "Transfers past the partition end are refused");
    }
    delete partition;
    delete disk;
    TEST_END();
}

namespace blk_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_find_device_by_index();
    test_find_device_by_name();
    test_find_device_by_type();
    test_gpt_data_partitions();

    TEST_SUMMARY("Block Manager");
}

}  // namespace blk_test
