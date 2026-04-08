#pragma once

#include <base/types.h>

#define MBR_SECTOR_SIZE      512    /* MBR sector size */
#define MBR_SIGNATURE        0xAA55 /* MBR signature */
#define MBR_PARTITION_COUNT  4      /* Number of partition entries */
#define MBR_PARTITION_OFFSET 446    /* Offset of partition table in MBR */

#define PARTITION_BOOTABLE     0x80 /* Bootable (active partition) */
#define PARTITION_NOT_BOOTABLE 0x00 /* Not bootable */

#define PART_TYPE_EMPTY       0x00 /* Empty partition */
#define PART_TYPE_FAT12       0x01 /* FAT12 */
#define PART_TYPE_FAT16_SMALL 0x04 /* FAT16 < 32MB */
#define PART_TYPE_EXTENDED    0x05 /* Extended partition */
#define PART_TYPE_FAT16       0x06 /* FAT16 >= 32MB */
#define PART_TYPE_NTFS        0x07 /* NTFS */
#define PART_TYPE_FAT32       0x0B /* FAT32 */
#define PART_TYPE_FAT32_LBA   0x0C /* FAT32 with LBA */
#define PART_TYPE_FAT16_LBA   0x0E /* FAT16 with LBA */
#define PART_TYPE_LINUX_SWAP  0x82 /* Linux swap */
#define PART_TYPE_LINUX       0x83 /* Linux native */
#define PART_TYPE_LINUX_EXT   0x85 /* Linux extended */

#define VBR_LOAD_ADDRESS 0x7C00 /* VBR load address */
#define VBR_SECTOR_SIZE  512    /* VBR sector size */

#ifndef __ASSEMBLER__

struct MbrPartition {
    uint8_t boot_flag;    /* 0x80 = bootable, 0x00 = not bootable */
    uint8_t start_chs[3]; /* Starting CHS address */
    uint8_t type;         /* Partition type */
    uint8_t end_chs[3];   /* Ending CHS address */
    uint32_t start_lba;   /* Starting LBA address */
    uint32_t size;        /* Size in sectors */

    [[nodiscard]] inline bool is_gpt() const { return type == GPT_PROTECTIVE_MBR_TYPE; }
    [[nodiscard]] inline bool is_fat32() const { return type == PART_TYPE_FAT32 || type == PART_TYPE_FAT32_LBA; }

} __attribute__((packed));

struct MbrHeader {
    uint8_t boot_code[446];
    MbrPartition partitions[MBR_PARTITION_COUNT];
    uint16_t signature; /* 0xAA55 */

    [[nodiscard]] inline bool is_valid() const { return signature == MBR_SIGNATURE; }

} __attribute__((packed));

#endif /* __ASSEMBLER__ */
