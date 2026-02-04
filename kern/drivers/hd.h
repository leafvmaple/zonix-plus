#pragma once

#include <base/types.h>

#include "blk.h"

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

struct TaskStruct;

struct IdeConfig {
    uint8_t channel;
    uint8_t drive;
    uint16_t base;
    uint16_t ctrl;
    uint8_t irq;
    const char *name;
};

struct DiskInfo {
    uint32_t size{};                      // Size in sectors
    uint16_t cylinders{};                 // Number of cylinders
    uint16_t heads{};                     // Number of heads
    uint16_t sectors{};                   // Sectors per track
    int valid{};                          // Device is valid
};

struct IdeRequest {
    enum class Op : int { None = 0, Read = 1, Write = 2 };
    
    volatile int done{};                   // Set to 1 by ISR when operation completes
    volatile int err{};                    // Error flag set by ISR
    uint8_t* buffer{};                     // Pointer to buffer for current transfer
    Op op{Op::None};                       // Operation type
    TaskStruct* waiting{};                 // Sleeping task waiting for completion
    
    void reset() {
        done = 0;
        err = 0;
        buffer = nullptr;
        op = Op::None;
        waiting = nullptr;
    }
};

// IDE device structure
struct IdeDevice : public BlockDevice {
    int m_present{};                       // Device is present
    const IdeConfig* m_config{};           // Pointer to device configuration
    DiskInfo m_info{};                     // Disk information
    IdeRequest m_request{};                // Current I/O request state

    void detect(const IdeConfig* config);
    void interupt();

    int read(uint32_t blockNumber, void* buf, size_t blockCount) override;
    int write(uint32_t blockNumber, const void* buf, size_t blockCount) override;
};

// IDE device manager class
class IdeManager {
public:
    static void init();

    static IdeDevice* get_device(int deviceID);
    static int get_device_count();

    static void interrupt_handler(int channel);

    // Test
    static void test();
    static void test_interrupt();

private:
    static IdeDevice s_ide_devices[MAX_IDE_DEVICES];
    static int s_ide_devices_count;

    static IdeConfig s_ide_configs[MAX_IDE_DEVICES];
};
