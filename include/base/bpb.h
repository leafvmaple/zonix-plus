#pragma once

#include <base/types.h>

/* FAT BIOS Parameter Block (BPB) related definitions */

/* Standard values for FAT */
#define BPB_BYTES_PER_SECTOR    512
#define BPB_SECTORS_PER_CLUSTER 8       /* 4KB clusters for FAT32 */
#define BPB_RESERVED_SECTORS_16 1       /* Only boot sector for FAT16 */
#define BPB_RESERVED_SECTORS_32 32      /* Multiple sectors for FAT32 */
#define BPB_NUM_FATS            2       /* Two FAT copies */
#define BPB_ROOT_ENTRIES_16     224     /* Standard for FAT16 (0 for FAT32) */
#define BPB_ROOT_ENTRIES_32     0       /* FAT32 uses cluster chain for root */
#define BPB_MEDIA_TYPE          0xF8    /* Fixed disk */
#define BPB_SECTORS_PER_TRACK   63      /* Standard CHS */
#define BPB_NUM_HEADS           16      /* Standard CHS */
#define BPB_HIDDEN_SECTORS      0
#define BPB_DRIVE_NUMBER        0x80    /* First hard disk */
#define BPB_BOOT_SIGNATURE      0x29    /* Extended boot signature */

/* FAT specific signatures */
#define FAT16_SIGNATURE         "FAT16   "
#define FAT32_SIGNATURE         "FAT32   "
#define OEM_NAME                "ZONIX   "
#define VOLUME_LABEL            "ZONIX      "  /* 11 chars */

/* Kernel file name in 8.3 format */
#define KERNEL_NAME             "KERNEL  SYS"

/* Memory layout for bootloader */
#define VBR_LOAD_ADDRESS        0x7C00  /* Where BIOS loads VBR */
#define VBR_STACK_ADDRESS       0x7C00  /* Stack grows down from here */
#define ROOT_DIR_BUFFER         0x0500  /* Load root directory here (0x500:0000) */
#define KERNEL_LOAD_ADDRESS     0x10000 /* Load kernel at 64KB (0x1000:0000) */
#define FAT_BUFFER_ADDRESS      0x0800  /* FAT table buffer (0x800:0000) */

/* Boot signature */
#define BOOT_SIGNATURE          0xAA55

/* FAT directory entry offsets */
#define DIR_ENTRY_SIZE          32
#define DIR_ENTRY_NAME          0
#define DIR_ENTRY_ATTR          11
#define DIR_ENTRY_CLUSTER_HI    20      /* High word of cluster (FAT32) */
#define DIR_ENTRY_CLUSTER_LO    26      /* Low word of cluster */
#define DIR_ENTRY_SIZE_OFFSET   28

/* FAT special values */
#define FAT16_EOC               0xFFF8  /* End of chain marker */
#define FAT32_EOC               0x0FFFFFF8  /* End of chain marker for FAT32 */
#define FAT32_EOC_MASK          0x0FFFFFFF  /* Mask for FAT32 entries */

/* Boot error codes */
#define ERROR_NO_KERNEL         0x01
#define ERROR_DISK_READ         0x02
#define ERROR_BAD_KERNEL        0x03

/* FAT directory entry attributes */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F  /* Long file name */

#ifndef __ASSEMBLER__
/* FAT directory entry (32 bytes) - unified structure */
typedef struct {
    char     name[8];               /* 0x00: File name (space padded) */
    char     ext[3];                /* 0x08: Extension */
    uint8_t  attr;                  /* 0x0B: Attributes */
    uint8_t  nt_reserved;           /* 0x0C: Reserved (NT) */
    uint8_t  create_time_tenth;     /* 0x0D: Creation time (1/10 second) */
    uint16_t create_time;           /* 0x0E: Creation time */
    uint16_t create_date;           /* 0x10: Creation date */
    uint16_t last_access_date;      /* 0x12: Last access date */
    uint16_t first_cluster_high;    /* 0x14: High word of first cluster (FAT32) */
    uint16_t write_time;            /* 0x16: Last write time */
    uint16_t write_date;            /* 0x18: Last write date */
    uint16_t first_cluster_low;     /* 0x1A: Low word of first cluster */
    uint32_t file_size;             /* 0x1C: File size in bytes */
} __attribute__((packed)) fat_dir_entry_t;

/* Common FAT16/FAT32 BPB structure (shared part) */
typedef struct {
    uint16_t bytes_per_sector;      /* Offset 0x0B */
    uint8_t  sectors_per_cluster;   /* Offset 0x0D */
    uint16_t reserved_sectors;      /* Offset 0x0E */
    uint8_t  num_fats;              /* Offset 0x10 */
    uint16_t root_entries;          /* Offset 0x11 (0 for FAT32) */
    uint16_t total_sectors_16;      /* Offset 0x13 (0 for FAT32) */
    uint8_t  media_type;            /* Offset 0x15 */
    uint16_t fat_size_16;           /* Offset 0x16 (0 for FAT32) */
    uint16_t sectors_per_track;     /* Offset 0x18 */
    uint16_t num_heads;             /* Offset 0x1A */
    uint32_t hidden_sectors;        /* Offset 0x1C */
    uint32_t total_sectors_32;      /* Offset 0x20 */
} __attribute__((packed)) fat_bpb_common_t;

/* FAT16 Extended BPB */
typedef struct {
    fat_bpb_common_t common;
    uint8_t  drive_number;          /* Offset 0x24 */
    uint8_t  reserved1;             /* Offset 0x25 */
    uint8_t  boot_signature;        /* Offset 0x26 (0x29) */
    uint32_t volume_id;             /* Offset 0x27 */
    char     volume_label[11];      /* Offset 0x2B */
    char     fs_type[8];            /* Offset 0x36 "FAT16   " */
} __attribute__((packed)) fat16_bpb_t;

/* FAT32 Extended BPB */
typedef struct {
    fat_bpb_common_t common;
    uint32_t fat_size_32;           /* Offset 0x24: FAT size in sectors */
    uint16_t ext_flags;             /* Offset 0x28: Extended flags */
    uint16_t fs_version;            /* Offset 0x2A: File system version */
    uint32_t root_cluster;          /* Offset 0x2C: Root directory cluster */
    uint16_t fs_info;               /* Offset 0x30: FSInfo sector */
    uint16_t backup_boot_sector;    /* Offset 0x32: Backup boot sector */
    uint8_t  reserved[12];          /* Offset 0x34: Reserved */
    uint8_t  drive_number;          /* Offset 0x40 */
    uint8_t  reserved1;             /* Offset 0x41 */
    uint8_t  boot_signature;        /* Offset 0x42 (0x29) */
    uint32_t volume_id;             /* Offset 0x43 */
    char     volume_label[11];      /* Offset 0x47 */
    char     fs_type[8];            /* Offset 0x52 "FAT32   " */
} __attribute__((packed)) fat32_bpb_t;

/* Complete boot sector structures for kernel use */

/* Boot sector structure (FAT12/FAT16) */
struct fat16_boot_sector {
    uint8_t  jmp[3];                /* 0x00: Jump instruction */
    char     oem[8];                /* 0x03: OEM name */
    uint16_t bytes_per_sector;      /* 0x0B: Bytes per sector (512) */
    uint8_t  sectors_per_cluster;   /* 0x0D: Sectors per cluster */
    uint16_t reserved_sectors;      /* 0x0E: Reserved sectors (1) */
    uint8_t  num_fats;              /* 0x10: Number of FAT tables (2) */
    uint16_t root_entries;          /* 0x11: Root directory entries (224) */
    uint16_t total_sectors_16;      /* 0x13: Total sectors (<32MB) */
    uint8_t  media_type;            /* 0x15: Media type (0xF8=hard disk) */
    uint16_t fat_size_16;           /* 0x16: FAT size in sectors */
    uint16_t sectors_per_track;     /* 0x18: Sectors per track */
    uint16_t num_heads;             /* 0x1A: Number of heads */
    uint32_t hidden_sectors;        /* 0x1C: Hidden sectors */
    uint32_t total_sectors_32;      /* 0x20: Total sectors (>32MB) */
    
    /* Extended boot record (FAT12/FAT16) */
    uint8_t  drive_number;          /* 0x24: Drive number */
    uint8_t  reserved1;             /* 0x25: Reserved */
    uint8_t  boot_signature;        /* 0x26: Extended boot signature (0x29) */
    uint32_t volume_id;             /* 0x27: Volume serial number */
    char     volume_label[11];      /* 0x2B: Volume label */
    char     fs_type[8];            /* 0x36: File system type "FAT12   " or "FAT16   " */
    
    uint8_t  boot_code[448];        /* 0x3E: Boot code */
    uint16_t boot_signature_word;   /* 0x1FE: Boot signature (0xAA55) */
} __attribute__((packed));

/* Boot sector structure (FAT32) */
struct fat32_boot_sector {
    uint8_t  jmp[3];                /* 0x00: Jump instruction */
    char     oem[8];                /* 0x03: OEM name */
    uint16_t bytes_per_sector;      /* 0x0B: Bytes per sector (512) */
    uint8_t  sectors_per_cluster;   /* 0x0D: Sectors per cluster */
    uint16_t reserved_sectors;      /* 0x0E: Reserved sectors (usually 32) */
    uint8_t  num_fats;              /* 0x10: Number of FAT tables (2) */
    uint16_t root_entries;          /* 0x11: Must be 0 for FAT32 */
    uint16_t total_sectors_16;      /* 0x13: Must be 0 for FAT32 */
    uint8_t  media_type;            /* 0x15: Media type (0xF8=hard disk) */
    uint16_t fat_size_16;           /* 0x16: Must be 0 for FAT32 */
    uint16_t sectors_per_track;     /* 0x18: Sectors per track */
    uint16_t num_heads;             /* 0x1A: Number of heads */
    uint32_t hidden_sectors;        /* 0x1C: Hidden sectors */
    uint32_t total_sectors_32;      /* 0x20: Total sectors */
    
    /* FAT32 Extended boot record */
    uint32_t fat_size_32;           /* 0x24: FAT size in sectors */
    uint16_t ext_flags;             /* 0x28: Extended flags */
    uint16_t fs_version;            /* 0x2A: File system version */
    uint32_t root_cluster;          /* 0x2C: Root directory cluster */
    uint16_t fs_info;               /* 0x30: FSInfo sector number */
    uint16_t backup_boot_sector;    /* 0x32: Backup boot sector */
    uint8_t  reserved[12];          /* 0x34: Reserved */
    uint8_t  drive_number;          /* 0x40: Drive number */
    uint8_t  reserved1;             /* 0x41: Reserved */
    uint8_t  boot_signature;        /* 0x42: Extended boot signature (0x29) */
    uint32_t volume_id;             /* 0x43: Volume serial number */
    char     volume_label[11];      /* 0x47: Volume label */
    char     fs_type[8];            /* 0x52: File system type "FAT32   " */
    
    uint8_t  boot_code[420];        /* 0x5A: Boot code */
    uint16_t boot_signature_word;   /* 0x1FE: Boot signature (0xAA55) */
} __attribute__((packed));

#endif /* __ASSEMBLER__ */
