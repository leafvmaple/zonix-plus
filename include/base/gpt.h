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

    bool operator==(const GptGuid& other) const {
        return __builtin_memcmp(this, &other, sizeof(GptGuid)) == 0;
    }
    bool operator!=(const GptGuid& other) const { return !(*this == other); }
} __attribute__((packed));

inline constexpr GptGuid ESP_GUID = {
    0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
};

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
} __attribute__((packed));

struct GptPartitionEntry {
    GptGuid type_guid;      // Partition type GUID
    GptGuid unique_guid;    // Unique partition GUID
    uint64_t starting_lba;  // First LBA
    uint64_t ending_lba;    // Last LBA (inclusive)
    uint64_t attributes;    // Attribute flags
    uint16_t name[36];      // Partition name (UTF-16LE)
} __attribute__((packed));

inline bool is_esp_guid(const GptGuid* guid) {
    return *guid == ESP_GUID;
}

#endif /* __ASSEMBLER__ */
