#pragma once

#include <base/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// IDE/ATA disk constants
namespace ide {

inline constexpr size_t SECTOR_SIZE   = 512;    // Bytes per sector
inline constexpr uint16_t IDE0_BASE   = 0x1F0;  // Primary IDE controller base
inline constexpr uint16_t IDE1_BASE   = 0x170;  // Secondary IDE controller base
inline constexpr uint16_t IDE0_CTRL   = 0x3F6;  // Primary IDE control register
inline constexpr uint16_t IDE1_CTRL   = 0x376;  // Secondary IDE control register
inline constexpr int MAX_DEVICES      = 4;      // Maximum IDE devices (2 channels × 2 drives)

// IDE registers (relative to base)
inline constexpr int REG_DATA         = 0x0;    // Data register
inline constexpr int REG_ERROR        = 0x1;    // Error register (read)
inline constexpr int REG_FEATURES     = 0x1;    // Features register (write)
inline constexpr int REG_SECTOR_COUNT = 0x2;    // Sector count
inline constexpr int REG_LBA_LOW      = 0x3;    // LBA bits 0-7
inline constexpr int REG_LBA_MID      = 0x4;    // LBA bits 8-15
inline constexpr int REG_LBA_HIGH     = 0x5;    // LBA bits 16-23
inline constexpr int REG_DEVICE       = 0x6;    // Device select
inline constexpr int REG_STATUS       = 0x7;    // Status register (read)
inline constexpr int REG_COMMAND      = 0x7;    // Command register (write)

// IDE control register bits
inline constexpr uint8_t CTRL_nIEN = 0x02;      // Disable interrupts (set to disable)
inline constexpr uint8_t CTRL_SRST = 0x04;      // Software reset
inline constexpr uint8_t CTRL_HOB  = 0x80;      // High order byte

// IDE status bits
inline constexpr uint8_t STATUS_BSY  = 0x80;    // Busy
inline constexpr uint8_t STATUS_DRDY = 0x40;    // Drive ready
inline constexpr uint8_t STATUS_DF   = 0x20;    // Drive fault
inline constexpr uint8_t STATUS_DSC  = 0x10;    // Drive seek complete
inline constexpr uint8_t STATUS_DRQ  = 0x08;    // Data request
inline constexpr uint8_t STATUS_ERR  = 0x01;    // Error

// IDE commands
inline constexpr uint8_t CMD_READ     = 0x20;   // Read sectors
inline constexpr uint8_t CMD_WRITE    = 0x30;   // Write sectors
inline constexpr uint8_t CMD_IDENTIFY = 0xEC;   // Identify device

// Device selection
inline constexpr uint8_t DEV_MASTER = 0xE0;     // Master device (LBA mode)
inline constexpr uint8_t DEV_SLAVE  = 0xF0;     // Slave device (LBA mode)

inline constexpr size_t NAME_LEN = 8;           // Device name length

} // namespace ide

// Legacy compatibility
#define SECTOR_SIZE         ide::SECTOR_SIZE
#define IDE0_BASE           ide::IDE0_BASE
#define IDE1_BASE           ide::IDE1_BASE
#define IDE0_CTRL           ide::IDE0_CTRL
#define IDE1_CTRL           ide::IDE1_CTRL
#define MAX_IDE_DEVICES     ide::MAX_DEVICES
#define IDE_DATA            ide::REG_DATA
#define IDE_ERROR           ide::REG_ERROR
#define IDE_FEATURES        ide::REG_FEATURES
#define IDE_SECTOR_COUNT    ide::REG_SECTOR_COUNT
#define IDE_LBA_LOW         ide::REG_LBA_LOW
#define IDE_LBA_MID         ide::REG_LBA_MID
#define IDE_LBA_HIGH        ide::REG_LBA_HIGH
#define IDE_DEVICE          ide::REG_DEVICE
#define IDE_STATUS          ide::REG_STATUS
#define IDE_COMMAND         ide::REG_COMMAND
#define IDE_CTRL_nIEN       ide::CTRL_nIEN
#define IDE_CTRL_SRST       ide::CTRL_SRST
#define IDE_CTRL_HOB        ide::CTRL_HOB
#define IDE_BSY             ide::STATUS_BSY
#define IDE_DRDY            ide::STATUS_DRDY
#define IDE_DF              ide::STATUS_DF
#define IDE_DSC             ide::STATUS_DSC
#define IDE_DRQ             ide::STATUS_DRQ
#define IDE_ERR             ide::STATUS_ERR
#define IDE_CMD_READ        ide::CMD_READ
#define IDE_CMD_WRITE       ide::CMD_WRITE
#define IDE_CMD_IDENTIFY    ide::CMD_IDENTIFY
#define IDE_DEV_MASTER      ide::DEV_MASTER
#define IDE_DEV_SLAVE       ide::DEV_SLAVE
#define IDE_NAME_LEN        ide::NAME_LEN

// Disk info structure
struct DiskInfo {
    uint32_t size;                      // Size in sectors
    uint16_t cylinders;                 // Number of cylinders
    uint16_t heads;                     // Number of heads
    uint16_t sectors;                   // Sectors per track
    int valid;                          // Device is valid
};

using disk_info_t = DiskInfo;

struct TaskStruct;  // Forward declaration

// IDE device structure
struct IdeDevice {
    uint8_t channel;                    // 0 = primary, 1 = secondary
    uint8_t drive;                      // 0 = master, 1 = slave
    uint16_t base;                      // Base I/O port
    uint16_t ctrl;                      // Control register port
    uint8_t irq;                        // IRQ number
    DiskInfo info;                      // Disk information
    int present;                        // Device is present
    char name[ide::NAME_LEN];           // Device name (hda, hdb, hdc, hdd)
    
    // Fields used for interrupt-driven I/O
    volatile int irq_done;              // Set to 1 by ISR when operation completes
    volatile int err;                   // Error flag set by ISR
    void* buffer;                       // Pointer to buffer for current transfer (one sector)
    int op;                             // Operation type: 0=none, 1=read, 2=write
    TaskStruct* waiting;                // Sleeping task waiting for completion
};

// Function declarations - Multi-device API
void hd_init();
int hd_read_device(int dev_id, uint32_t secno, void* dst, size_t nsecs);
int hd_write_device(int dev_id, uint32_t secno, const void* src, size_t nsecs);
IdeDevice* hd_get_device(int dev_id);
int hd_get_device_count();

// Test functions
void hd_test();
void hd_test_interrupt();

// IDE interrupt handler (called by trap handler with IRQ_IDE1 or IRQ_IDE2)
void hd_intr(int irq);

#ifdef __cplusplus
}
#endif