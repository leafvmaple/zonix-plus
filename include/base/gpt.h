#pragma once

/**
 * GPT (GUID Partition Table) structures.
 *
 * A GPT disk has:
 *   LBA 0  — Protective MBR (type 0xEE)
 *   LBA 1  — GPT Header
 *   LBA 2+ — Partition Entry Array (typically 128 entries × 128 bytes = 32 sectors)
 */

#include <base/types.h>

#ifndef __ASSEMBLER__

inline constexpr uint8_t GPT_PROTECTIVE_MBR_TYPE = 0xEE;
inline constexpr uint64_t GPT_HEADER_SIGNATURE = 0x5452415020494645ULL;

struct GptGuid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];

    bool operator==(const GptGuid& other) const { return __builtin_memcmp(this, &other, sizeof(GptGuid)) == 0; }

    bool operator!=(const GptGuid& other) const { return !(*this == other); }

} __attribute__((packed));

inline constexpr GptGuid ESP_GUID = {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};

struct GptPartitionEntry {
    GptGuid type_guid;      // Partition type GUID
    GptGuid unique_guid;    // Unique partition GUID
    uint64_t starting_lba;  // First LBA
    uint64_t ending_lba;    // Last LBA (inclusive)
    uint64_t attributes;    // Attribute flags
    uint16_t name[36];      // Partition name (UTF-16LE)

    [[nodiscard]] inline bool is_empty() const {
        return type_guid.data1 == 0 && type_guid.data2 == 0 && type_guid.data3 == 0 &&
               __builtin_memcmp(type_guid.data4, "\0\0\0\0\0\0\0\0", 8) == 0;
    }

    [[nodiscard]] inline bool is_esp() const {  // EFI System Partition
        return type_guid == ESP_GUID;
    }
} __attribute__((packed));

struct GptHeader {
    uint64_t signature;              // "EFI PART" = 0x5452415020494645
    uint32_t revision;               // GPT revision (usually 0x00010000)
    uint32_t header_size;            // Size of this header (usually 92)
    uint32_t header_crc32;           // CRC32 of header (with this field zeroed)
    uint32_t reserved;               // Must be zero
    uint64_t my_lba;                 // LBA of this header (usually 1)
    uint64_t alternate_lba;          // LBA of backup header
    uint64_t first_usable_lba;       // First usable LBA for partitions
    uint64_t last_usable_lba;        // Last usable LBA for partitions
    GptGuid disk_guid;               // Disk GUID
    uint64_t partition_entry_lba;    // Start LBA of partition entry array
    uint32_t num_partition_entries;  // Number of partition entries
    uint32_t partition_entry_size;   // Size of each entry (usually 128)
    uint32_t partition_array_crc32;  // CRC32 of partition entry array

    [[nodiscard]] bool is_valid() const { return signature == GPT_HEADER_SIGNATURE; }

    template<typename Reader>
    [[nodiscard]] int32_t find_esp_lba(Reader reader) const {
        uint8_t buf[512]{};
        const uint32_t entries_per_sector = 512 / partition_entry_size;

        for (uint32_t i = 0; i < num_partition_entries; i++) {
            uint32_t sector = static_cast<uint32_t>(partition_entry_lba) + (i / entries_per_sector);
            uint32_t offset = (i % entries_per_sector) * partition_entry_size;

            if (reader(sector, buf) != 0) {
                break;
            }

            const auto* entry = reinterpret_cast<const GptPartitionEntry*>(buf + offset);
            if (entry->is_empty()) {
                continue;
            }

            if (entry->is_esp()) {
                return static_cast<int32_t>(entry->starting_lba);
            }
        }

        return -1;
    }
} __attribute__((packed));

#endif /* __ASSEMBLER__ */
