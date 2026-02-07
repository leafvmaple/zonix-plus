#pragma once

#include <base/types.h>
#include "../block/blk.h"

// AHCI (Advanced Host Controller Interface) constants and definitions
namespace ahci {

inline constexpr size_t SECTOR_SIZE = 512;        // Bytes per sector
inline constexpr int MAX_DEVICES = 4;             // Maximum AHCI ports/devices

// AHCI controller PCI configuration
inline constexpr uint32_t AHCI_BAR_BASE = 0xFEBF1000;  // Default MMIO base (may vary)
inline constexpr size_t AHCI_BAR_SIZE = 0x1000;        // Standard AHCI bar size

// AHCI register offsets (HBA memory registers)
inline constexpr uint32_t AHCI_CAP = 0x00;       // Host Capabilities
inline constexpr uint32_t AHCI_GHC = 0x04;       // Global Host Control
inline constexpr uint32_t AHCI_IS = 0x08;        // Interrupt Status
inline constexpr uint32_t AHCI_PI = 0x0C;        // Ports Implemented
inline constexpr uint32_t AHCI_VS = 0x10;        // Version

// AHCI_GHC bits (Global Host Control)
inline constexpr uint32_t GHC_AHCI_EN = 0x80000000;  // AHCI enable
inline constexpr uint32_t GHC_MRSM = 0x00000002;     // MSI Revert to Single Message
inline constexpr uint32_t GHC_IE = 0x00000001;       // Interrupt Enable

// Port registers (offset from port base)
inline constexpr uint32_t PORT_CMD = 0x18;      // Command and status
inline constexpr uint32_t PORT_CMDL = 0x18;     // Command list base address low
inline constexpr uint32_t PORT_CMDH = 0x1C;     // Command list base address high
inline constexpr uint32_t PORT_RFB = 0x20;      // Received FIS base address
inline constexpr uint32_t PORT_RFBL = 0x20;     // Received FIS base address low
inline constexpr uint32_t PORT_RFBH = 0x24;     // Received FIS base address high
inline constexpr uint32_t PORT_IS = 0x10;       // Interrupt status
inline constexpr uint32_t PORT_IE = 0x14;       // Interrupt enable
inline constexpr uint32_t PORT_CMD_STAT = 0x18; // Command and status
inline constexpr uint32_t PORT_SATA_CTL = 0x2C; // SATA control
inline constexpr uint32_t PORT_SATA_STS = 0x28; // SATA status
inline constexpr uint32_t PORT_TFD = 0x20;      // Task file data

// Port command and status register bits
inline constexpr uint32_t CMD_ST = 0x0001;      // Start (command processing)
inline constexpr uint32_t CMD_FRE = 0x0010;     // FIS receive enable
inline constexpr uint32_t CMD_FR = 0x4000;      // FIS receive running
inline constexpr uint32_t CMD_CR = 0x8000;      // Command list running

// Port interrupt status bits
inline constexpr uint32_t IS_DHRS = 0x00000001; // D2H register FIS received
inline constexpr uint32_t IS_PSS = 0x00000002;  // PIO Setup FIS received
inline constexpr uint32_t IS_DSS = 0x00000004;  // DMA Setup FIS received
inline constexpr uint32_t IS_SDBS = 0x00000008; // Set Device Bits received
inline constexpr uint32_t IS_UFS = 0x00000010;  // Unknown FIS received
inline constexpr uint32_t IS_DPS = 0x00000020;  // Descriptor processed
inline constexpr uint32_t IS_PCS = 0x00000040;  // Port connect change
inline constexpr uint32_t IS_DMPS = 0x00000080; // Device mechanical presence
inline constexpr uint32_t IS_PRCS = 0x00400000; // PhyRdy change
inline constexpr uint32_t IS_IPMS = 0x00800000; // Incorrect port multiplier
inline constexpr uint32_t IS_OFS = 0x01000000;  // Overflow

// SATA status register bits
inline constexpr uint32_t SATA_STS_DET_MASK = 0x0000000F;  // Device detection
inline constexpr uint32_t SATA_STS_DET_PRESENT = 0x00000003; // Device present
inline constexpr uint32_t SATA_STS_SPD_MASK = 0x000000F0;  // Current speed
inline constexpr uint32_t SATA_STS_IPM_MASK = 0x00000F00;  // Interface power mgmt

// ATA commands
inline constexpr uint8_t ATA_CMD_IDENTIFY = 0xEC;    // Identify device
inline constexpr uint8_t ATA_CMD_READ_DMA = 0xC8;    // Read DMA (28-bit)
inline constexpr uint8_t ATA_CMD_READ_DMA_EXT = 0x25; // Read DMA extended (48-bit)
inline constexpr uint8_t ATA_CMD_WRITE_DMA = 0xCA;   // Write DMA (28-bit)
inline constexpr uint8_t ATA_CMD_WRITE_DMA_EXT = 0x35; // Write DMA extended (48-bit)

// FIS types
inline constexpr uint8_t FIS_TYPE_REG_H2D = 0x27;   // Register FIS - host to device
inline constexpr uint8_t FIS_TYPE_REG_D2H = 0x34;   // Register FIS - device to host
inline constexpr uint8_t FIS_TYPE_DMA_ACT = 0x39;   // DMA activate FIS
inline constexpr uint8_t FIS_TYPE_DMA_SETUP = 0x41; // DMA setup FIS
inline constexpr uint8_t FIS_TYPE_DATA = 0x46;      // Data FIS
inline constexpr uint8_t FIS_TYPE_BIST = 0x58;      // BIST activate FIS
inline constexpr uint8_t FIS_TYPE_PIO_SETUP = 0x5F; // PIO setup FIS
inline constexpr uint8_t FIS_TYPE_DEV_BITS = 0xA1;  // Set device bits FIS

inline constexpr size_t NAME_LEN = 8;                // Device name length

} // namespace ahci

struct TaskStruct;

struct AhciPortConfig {
    uint8_t port_num{};
    uint16_t irq{};
    const char* name{};
};

struct AhciDeviceInfo {
    uint32_t size{};          // Size in sectors
    uint32_t serial{};        // Serial number
    uint32_t model{};         // Model number
    int valid{};              // Device is valid
};

struct AhciRequest {
    enum class Op : int { None = 0, Read = 1, Write = 2 };
    
    volatile int done{};      // Set to 1 by ISR when operation completes
    volatile int err{};       // Error flag set by ISR
    uint8_t* buffer{};        // Pointer to buffer for current transfer
    Op op{Op::None};          // Operation type
    TaskStruct* waiting{};    // Sleeping task waiting for completion
    
    void reset() {
        done = 0;
        err = 0;
        buffer = nullptr;
        op = Op::None;
        waiting = nullptr;
    }
};

// AHCI device structure (represents a SATA device on an AHCI port)
struct AhciDevice : public BlockDevice {
    int m_present{};                      // Device is present
    const AhciPortConfig* m_config{};     // Pointer to port configuration
    AhciDeviceInfo m_info{};              // Device information
    AhciRequest m_request{};              // Current I/O request state
    uint32_t m_port_base{};               // MMIO base address for this port

    void detect(const AhciPortConfig* config, uint32_t mmio_base);
    void interrupt();

    int read(uint32_t blockNumber, void* buf, size_t blockCount) override;
    int write(uint32_t blockNumber, const void* buf, size_t blockCount) override;
};

class AhciManager {
public:
    static void init();

    static AhciDevice* get_device(int deviceID);
    static int get_device_count();

    static void interrupt_handler(int port);

    // Test
    static void test();

private:
    static AhciDevice s_ahci_devices[ahci::MAX_DEVICES];
    static int s_ahci_devices_count;

    static AhciPortConfig s_ahci_port_configs[ahci::MAX_DEVICES];
    static uint32_t s_ahci_base;  // AHCI controller MMIO base
};
