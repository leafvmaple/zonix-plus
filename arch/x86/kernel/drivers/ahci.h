#pragma once

#include <base/types.h>
#include "block/blk.h"
#include "asm/trap_numbers.h"

namespace pci {
struct DeviceInfo;
struct DriverId;
}  // namespace pci

namespace ahci {

inline constexpr size_t SECTOR_SIZE = 512;  // Bytes per sector
inline constexpr int MAX_DEVICES = 4;       // Maximum AHCI ports/devices
inline constexpr size_t DMA_PAGE_SIZE = 4096;
inline constexpr size_t CMD_SLOT_COUNT = 32;

inline constexpr uint32_t AHCI_BAR_BASE = 0xFEBF1000;  // Default MMIO base (may vary)
inline constexpr size_t AHCI_BAR_SIZE = 0x10000;       // AHCI MMIO region size (64KB)

inline constexpr uint32_t AHCI_CAP = 0x00;  // Host Capabilities
inline constexpr uint32_t AHCI_GHC = 0x04;  // Global Host Control
inline constexpr uint32_t AHCI_IS = 0x08;   // Interrupt Status
inline constexpr uint32_t AHCI_PI = 0x0C;   // Ports Implemented
inline constexpr uint32_t AHCI_VS = 0x10;   // Version

inline constexpr uint32_t PORT_BASE_OFFSET = 0x100;  // Offset to first port's registers
inline constexpr uint32_t PORT_REG_SIZE = 0x80;      // Size of each port's register block (128 bytes)

inline constexpr uint32_t GHC_AHCI_EN = 0x80000000;  // AHCI enable
inline constexpr uint32_t GHC_MRSM = 0x00000002;     // MSI Revert to Single Message
inline constexpr uint32_t GHC_IE = 0x00000001;       // Interrupt Enable

inline constexpr uint32_t PORT_CMD = 0x18;       // Command and status
inline constexpr uint32_t PORT_CMDL = 0x18;      // Command list base address low
inline constexpr uint32_t PORT_CMDH = 0x1C;      // Command list base address high
inline constexpr uint32_t PORT_RFB = 0x20;       // Received FIS base address
inline constexpr uint32_t PORT_RFBL = 0x20;      // Received FIS base address low
inline constexpr uint32_t PORT_RFBH = 0x24;      // Received FIS base address high
inline constexpr uint32_t PORT_IS = 0x10;        // Interrupt status
inline constexpr uint32_t PORT_IE = 0x14;        // Interrupt enable
inline constexpr uint32_t PORT_CMD_STAT = 0x18;  // Command and status
inline constexpr uint32_t PORT_SATA_CTL = 0x2C;  // SATA control
inline constexpr uint32_t PORT_SATA_STS = 0x28;  // SATA status
inline constexpr uint32_t PORT_TFD = 0x20;       // Task file data

inline constexpr uint32_t CMD_ST = 0x0001;   // Start (command processing)
inline constexpr uint32_t CMD_FRE = 0x0010;  // FIS receive enable
inline constexpr uint32_t CMD_FR = 0x4000;   // FIS receive running
inline constexpr uint32_t CMD_CR = 0x8000;   // Command list running

inline constexpr uint32_t IS_DHRS = 0x00000001;  // D2H register FIS received
inline constexpr uint32_t IS_PSS = 0x00000002;   // PIO Setup FIS received
inline constexpr uint32_t IS_DSS = 0x00000004;   // DMA Setup FIS received
inline constexpr uint32_t IS_SDBS = 0x00000008;  // Set Device Bits received
inline constexpr uint32_t IS_UFS = 0x00000010;   // Unknown FIS received
inline constexpr uint32_t IS_DPS = 0x00000020;   // Descriptor processed
inline constexpr uint32_t IS_PCS = 0x00000040;   // Port connect change
inline constexpr uint32_t IS_DMPS = 0x00000080;  // Device mechanical presence
inline constexpr uint32_t IS_PRCS = 0x00400000;  // PhyRdy change
inline constexpr uint32_t IS_IPMS = 0x00800000;  // Incorrect port multiplier
inline constexpr uint32_t IS_OFS = 0x01000000;   // Overflow

inline constexpr uint32_t SATA_STS_DET_MASK = 0x0000000F;     // Device detection
inline constexpr uint32_t SATA_STS_DET_PRESENT = 0x00000003;  // Device present
inline constexpr uint32_t SATA_STS_SPD_MASK = 0x000000F0;     // Current speed
inline constexpr uint32_t SATA_STS_IPM_MASK = 0x00000F00;     // Interface power mgmt

inline constexpr uint8_t ATA_CMD_IDENTIFY = 0xEC;       // Identify device
inline constexpr uint8_t ATA_CMD_READ_DMA = 0xC8;       // Read DMA (28-bit)
inline constexpr uint8_t ATA_CMD_READ_DMA_EXT = 0x25;   // Read DMA extended (48-bit)
inline constexpr uint8_t ATA_CMD_WRITE_DMA = 0xCA;      // Write DMA (28-bit)
inline constexpr uint8_t ATA_CMD_WRITE_DMA_EXT = 0x35;  // Write DMA extended (48-bit)

inline constexpr uint8_t FIS_TYPE_REG_H2D = 0x27;    // Register FIS - host to device
inline constexpr uint8_t FIS_TYPE_REG_D2H = 0x34;    // Register FIS - device to host
inline constexpr uint8_t FIS_TYPE_DMA_ACT = 0x39;    // DMA activate FIS
inline constexpr uint8_t FIS_TYPE_DMA_SETUP = 0x41;  // DMA setup FIS
inline constexpr uint8_t FIS_TYPE_DATA = 0x46;       // Data FIS
inline constexpr uint8_t FIS_TYPE_BIST = 0x58;       // BIST activate FIS
inline constexpr uint8_t FIS_TYPE_PIO_SETUP = 0x5F;  // PIO setup FIS
inline constexpr uint8_t FIS_TYPE_DEV_BITS = 0xA1;   // Set device bits FIS

inline constexpr size_t NAME_LEN = 8;  // Device name length

inline constexpr uint32_t PORT_CLB = 0x00;   // Command list base address low
inline constexpr uint32_t PORT_CLBU = 0x04;  // Command list base address high
inline constexpr uint32_t PORT_FB = 0x08;    // FIS base address low
inline constexpr uint32_t PORT_FBU = 0x0C;   // FIS base address high
inline constexpr uint32_t PORT_CI = 0x38;    // Command issue

inline constexpr uint32_t TFD_STS_BSY = 0x80;  // Busy
inline constexpr uint32_t TFD_STS_DRQ = 0x08;  // Data transfer requested
inline constexpr uint32_t TFD_STS_ERR = 0x01;  // Error

}  // namespace ahci

// AHCI Command Header (32 bytes each, 32 slots = 1KB total)
struct AhciCmdHeader {
    uint8_t cfl : 5;       // Command FIS length in DWORDs (2-16)
    uint8_t atapi : 1;     // ATAPI command
    uint8_t write : 1;     // Write (1) or Read (0)
    uint8_t prefetch : 1;  // Prefetchable

    uint8_t reset : 1;  // Reset
    uint8_t bist : 1;   // BIST
    uint8_t clear : 1;  // Clear busy upon R_OK
    uint8_t rsv0 : 1;   // Reserved
    uint8_t pmp : 4;    // Port multiplier port

    uint16_t prdtl;  // Physical region descriptor table length (entries)
    uint32_t prdbc;  // PRD byte count transferred
    uint32_t ctba;   // Command table descriptor base address low
    uint32_t ctbau;  // Command table descriptor base address high
    uint32_t rsv1[4];
} __attribute__((packed));

// Physical Region Descriptor Table entry (16 bytes)
struct AhciPrdt {
    uint32_t dba{};   // Data base address low
    uint32_t dbau{};  // Data base address high
    uint32_t rsv0{};  // Reserved
    uint32_t dbc{};   // Byte count (bit 0 must be 1, max 4MB-1), bit 31 = interrupt on completion

    void set_data_buffer(uintptr_t buf_phys, uint32_t data_bytes);
} __attribute__((packed));

// Host to Device Register FIS (20 bytes)
struct FisRegH2D {
    uint8_t fis_type{};  // FIS_TYPE_REG_H2D
    uint8_t pmport : 4;
    uint8_t rsv0 : 3;
    uint8_t c : 1;  // Command (1) or Control (0)

    uint8_t command{};   // ATA command
    uint8_t featurel{};  // Feature low

    uint8_t lba0{};    // LBA 7:0
    uint8_t lba1{};    // LBA 15:8
    uint8_t lba2{};    // LBA 23:16
    uint8_t device{};  // Device register

    uint8_t lba3{};      // LBA 31:24
    uint8_t lba4{};      // LBA 39:32
    uint8_t lba5{};      // LBA 47:40
    uint8_t featureh{};  // Feature high

    uint8_t countl{};  // Sector count low
    uint8_t counth{};  // Sector count high
    uint8_t icc{};     // Isochronous command completion
    uint8_t control{};

    uint8_t rsv1[4]{};

    void set_command(uint8_t cmd, uint32_t lba, uint16_t count);
} __attribute__((packed));

struct AhciCmdTable {
    uint8_t cfis[64]{};  // Command FIS
    uint8_t acmd[16]{};  // ATAPI command (if needed)
    uint8_t rsv[48]{};   // Reserved
    AhciPrdt prdt[1]{};  // PRDT entries (variable, at least 1)
} __attribute__((packed));

static_assert(sizeof(AhciCmdHeader) == 32, "AhciCmdHeader must be 32 bytes");
static_assert(sizeof(AhciCmdTable) <= ahci::DMA_PAGE_SIZE, "AhciCmdTable must fit in one DMA page");

struct TaskStruct;

struct AhciPortConfig {
    uint8_t port_num{};
    uint16_t irq{};
    const char* name{};
};

struct AhciDeviceInfo {
    uint32_t size{};       // Size in sectors
    uint32_t serial{};     // Serial number
    uint32_t model{};      // Model number
    uint16_t cylinders{};  // CHS: cylinders
    uint16_t heads{};      // CHS: heads
    uint16_t sectors{};    // CHS: sectors per track
    int valid{};           // Device is valid
};

struct AhciRequest {
    enum class Op : int8_t { None = 0, Read = 1, Write = 2 };

    volatile int done{};    // Set to 1 by ISR when operation completes
    volatile int err{};     // Error flag set by ISR
    uint8_t* buffer{};      // Pointer to buffer for current transfer
    Op op{Op::None};        // Operation type
    TaskStruct* waiting{};  // Sleeping task waiting for completion

    void reset() {
        done = 0;
        err = 0;
        buffer = nullptr;
        op = Op::None;
        waiting = nullptr;
    }
};

struct AhciDevice : public BlockDevice {
    const AhciPortConfig* config{};  // Port configuration

    AhciDeviceInfo info{};  // Device information
    AhciRequest request{};  // Current I/O request state

    int detect(const AhciPortConfig* cfg, uintptr_t mmio_base);
    int setup_memory();
    int identify();
    void interrupt();

    int read(uint32_t block_number, void* buf, size_t block_count) override;
    int write(uint32_t block_number, const void* buf, size_t block_count) override;
    void print_info() override;

private:
    int issue_cmd(uint8_t command, uint32_t lba, uint16_t count, bool write);
    int wait_cmd_complete(int timeout_ms);
    int transfer_blocks(uint32_t block_number, size_t block_count, void* buf, bool write);

    int present_{};
    uintptr_t port_base_{};

    alignas(1024) AhciCmdHeader cmd_list_[ahci::CMD_SLOT_COUNT]{};
    alignas(256) uint8_t fis_base_[256]{};
    alignas(128) AhciCmdTable cmd_table_{};
    alignas(ahci::DMA_PAGE_SIZE) uint8_t dma_buf_[ahci::DMA_PAGE_SIZE]{};

    friend class AhciManager;
};

class AhciManager {
public:
    static int init();
    static int probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*);

    static AhciDevice* get_device(int device_id);
    static int get_device_count();

    static void interrupt_handler(int port);

private:
    inline static uintptr_t s_base_{};  // AHCI controller MMIO virtual base
    inline static AhciDevice s_devices_[ahci::MAX_DEVICES]{};
    inline static int s_devices_count_{};

    inline static bool s_ctrl_ready_{};
    inline static bool s_registered_{};

    inline static AhciPortConfig s_port_configs[ahci::MAX_DEVICES] = {
        {0, IRQ_IDE1, "sda"},  // AHCI port 0
        {1, IRQ_IDE1, "sdb"},  // AHCI port 1
        {2, IRQ_IDE1, "sdc"},  // AHCI port 2
        {3, IRQ_IDE1, "sdd"},  // AHCI port 3
    };
};
