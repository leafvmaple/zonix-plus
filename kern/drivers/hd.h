#pragma once

#include <base/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// IDE/ATA disk constants
#define SECTOR_SIZE         512         // Bytes per sector
#define IDE0_BASE           0x1F0       // Primary IDE controller base
#define IDE1_BASE           0x170       // Secondary IDE controller base
#define IDE0_CTRL           0x3F6       // Primary IDE control register
#define IDE1_CTRL           0x376       // Secondary IDE control register
#define MAX_IDE_DEVICES     4           // Maximum IDE devices (2 channels × 2 drives)

// IDE registers (relative to base)
#define IDE_DATA            0x0         // Data register
#define IDE_ERROR           0x1         // Error register (read)
#define IDE_FEATURES        0x1         // Features register (write)
#define IDE_SECTOR_COUNT    0x2         // Sector count
#define IDE_LBA_LOW         0x3         // LBA bits 0-7
#define IDE_LBA_MID         0x4         // LBA bits 8-15
#define IDE_LBA_HIGH        0x5         // LBA bits 16-23
#define IDE_DEVICE          0x6         // Device select
#define IDE_STATUS          0x7         // Status register (read)
#define IDE_COMMAND         0x7         // Command register (write)

// IDE control register bits
#define IDE_CTRL_nIEN       0x02        // Disable interrupts (set to disable)
#define IDE_CTRL_SRST       0x04        // Software reset
#define IDE_CTRL_HOB        0x80        // High order byte

// IDE status bits
#define IDE_BSY             0x80        // Busy
#define IDE_DRDY            0x40        // Drive ready
#define IDE_DF              0x20        // Drive fault
#define IDE_DSC             0x10        // Drive seek complete
#define IDE_DRQ             0x08        // Data request
#define IDE_ERR             0x01        // Error

// IDE commands
#define IDE_CMD_READ        0x20        // Read sectors
#define IDE_CMD_WRITE       0x30        // Write sectors
#define IDE_CMD_IDENTIFY    0xEC        // Identify device

// Device selection
#define IDE_DEV_MASTER      0xE0        // Master device (LBA mode)
#define IDE_DEV_SLAVE       0xF0        // Slave device (LBA mode)

#define IDE_NAME_LEN       8           // Device name length

// Disk info structure
typedef struct {
    uint32_t size;                      // Size in sectors
    uint16_t cylinders;                 // Number of cylinders
    uint16_t heads;                     // Number of heads
    uint16_t sectors;                   // Sectors per track
    int valid;                          // Device is valid
} disk_info_t;

// IDE device structure
typedef struct {
    uint8_t channel;                    // 0 = primary, 1 = secondary
    uint8_t drive;                      // 0 = master, 1 = slave
    uint16_t base;                      // Base I/O port
    uint16_t ctrl;                      // Control register port
    uint8_t irq;                        // IRQ number
    disk_info_t info;                   // Disk information
    int present;                        // Device is present
    char name[IDE_NAME_LEN];            // Device name (hda, hdb, hdc, hdd)
    
    // Fields used for interrupt-driven I/O
    volatile int irq_done;              // Set to 1 by ISR when operation completes
    volatile int err;                   // Error flag set by ISR
    void *buffer;                       // Pointer to buffer for current transfer (one sector)
    int op;                             // Operation type: 0=none, 1=read, 2=write
    struct task_struct *waiting;        // Sleeping task waiting for completion
} ide_device_t;

// Function declarations - Multi-device API
void hd_init(void);
int hd_read_device(int dev_id, uint32_t secno, void *dst, size_t nsecs);
int hd_write_device(int dev_id, uint32_t secno, const void *src, size_t nsecs);
ide_device_t *hd_get_device(int dev_id);
int hd_get_device_count(void);

// Test functions
void hd_test(void);
void hd_test_interrupt(void);

// IDE interrupt handler (called by trap handler with IRQ_IDE1 or IRQ_IDE2)
void hd_intr(int irq);

#ifdef __cplusplus
}
#endif