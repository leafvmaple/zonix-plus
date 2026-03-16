#include "sdhci.h"
#include "drivers/pci.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include <asm/page.h>

// ==========================================================================
// SDHCI register offsets (SD Host Controller Specification v3.0)
// ==========================================================================
namespace reg {

constexpr uint32_t SDMA_ADDR = 0x00;
constexpr uint32_t BLOCK_SIZE = 0x04;  // lower 16: block size; upper 16: block count
constexpr uint32_t BLOCK_COUNT = 0x06;
constexpr uint32_t ARGUMENT = 0x08;
constexpr uint32_t XFER_MODE = 0x0C;
constexpr uint32_t COMMAND = 0x0E;
constexpr uint32_t RESPONSE0 = 0x10;
constexpr uint32_t RESPONSE1 = 0x14;
constexpr uint32_t RESPONSE2 = 0x18;
constexpr uint32_t RESPONSE3 = 0x1C;
constexpr uint32_t BUF_DATA = 0x20;
constexpr uint32_t PRESENT_STATE = 0x24;
constexpr uint32_t HOST_CTRL1 = 0x28;
constexpr uint32_t POWER_CTRL = 0x29;
constexpr uint32_t CLOCK_CTRL = 0x2C;
constexpr uint32_t TIMEOUT_CTRL = 0x2E;
constexpr uint32_t SW_RESET = 0x2F;
constexpr uint32_t INT_STATUS = 0x30;
constexpr uint32_t ERR_STATUS = 0x32;
constexpr uint32_t INT_ENABLE = 0x34;
constexpr uint32_t ERR_ENABLE = 0x36;
constexpr uint32_t INT_SIGNAL = 0x38;
constexpr uint32_t ERR_SIGNAL = 0x3A;
constexpr uint32_t CAPABILITIES = 0x40;
constexpr uint32_t HOST_VERSION = 0xFE;

}  // namespace reg

// ==========================================================================
// Register bit definitions
// ==========================================================================

// Present State
constexpr uint32_t PS_CMD_INHIBIT = (1u << 0);
constexpr uint32_t PS_DAT_INHIBIT = (1u << 1);
constexpr uint32_t PS_BUF_READ_EN = (1u << 11);
constexpr uint32_t PS_BUF_WRITE_EN = (1u << 10);
constexpr uint32_t PS_CARD_INSERTED = (1u << 16);
constexpr uint32_t PS_CARD_STABLE = (1u << 17);

// Normal Interrupt Status
constexpr uint16_t INT_CMD_DONE = (1u << 0);
constexpr uint16_t INT_XFER_DONE = (1u << 1);
constexpr uint16_t INT_DMA = (1u << 3);
constexpr uint16_t INT_BUF_WR_READY = (1u << 4);
constexpr uint16_t INT_BUF_RD_READY = (1u << 5);
constexpr uint16_t INT_ERROR = (1u << 15);

// Clock Control
constexpr uint16_t CLK_INT_EN = (1u << 0);
constexpr uint16_t CLK_INT_STABLE = (1u << 1);
constexpr uint16_t CLK_SD_EN = (1u << 2);

// Software Reset
constexpr uint8_t RST_ALL = (1u << 0);
constexpr uint8_t RST_CMD = (1u << 1);
constexpr uint8_t RST_DAT = (1u << 2);

// Power Control
constexpr uint8_t PWR_ON = (1u << 0);
constexpr uint8_t PWR_3V3 = (7u << 1);  // 3.3V

// Command register flags (bits [7:0] of the 16-bit command register)
constexpr uint16_t CMD_RESP_NONE = 0x00;
constexpr uint16_t CMD_RESP_136 = 0x01;      // R2
constexpr uint16_t CMD_RESP_48 = 0x02;       // R1, R3, R6, R7
constexpr uint16_t CMD_RESP_48_BUSY = 0x03;  // R1b
constexpr uint16_t CMD_CRC_EN = 0x08;
constexpr uint16_t CMD_IDX_EN = 0x10;
constexpr uint16_t CMD_DATA = 0x20;

// Transfer Mode
constexpr uint16_t XFER_READ = (1u << 4);
constexpr uint16_t XFER_MULTI = (1u << 5);
constexpr uint16_t XFER_BLK_CNT_EN = (1u << 1);

// SD command indices
constexpr uint8_t SD_CMD0_GO_IDLE = 0;
constexpr uint8_t SD_CMD2_ALL_CID = 2;
constexpr uint8_t SD_CMD3_SEND_RCA = 3;
constexpr uint8_t SD_CMD7_SELECT = 7;
constexpr uint8_t SD_CMD8_SEND_IF = 8;
constexpr uint8_t SD_CMD16_SET_BLKLEN = 16;
constexpr uint8_t SD_CMD17_READ = 17;
constexpr uint8_t SD_CMD24_WRITE = 24;
constexpr uint8_t SD_CMD55_APP = 55;
constexpr uint8_t SD_ACMD41_OP_COND = 41;

// ACMD41 argument
constexpr uint32_t ACMD41_HCS = (1u << 30);      // Host Capacity Support (SDHC)
constexpr uint32_t ACMD41_VOLTAGE = 0x00FF8000;  // 2.7-3.6V

// ACMD41 response
constexpr uint32_t OCR_BUSY = (1u << 31);  // Card power-up status (ready)
constexpr uint32_t OCR_CCS = (1u << 30);   // Card Capacity Status (SDHC)

// Retry limits
constexpr int CMD_TIMEOUT_US = 1000000;  // 1 second in busy-loop iterations
constexpr int ACMD41_RETRIES = 100;

// SDHCI PCI class code: System peripheral (0x08), SD Host (0x05)
constexpr uint8_t PCI_CLASS_SYSTEM = 0x08;
constexpr uint8_t PCI_SUBCLASS_SD = 0x05;

// ==========================================================================
// Static device instance
// ==========================================================================

static SdDevice s_sd_device;

// ==========================================================================
// Register access
// ==========================================================================

uint32_t SdDevice::rd32(uint32_t off) {
    return *reinterpret_cast<volatile uint32_t*>(m_base + off);
}

void SdDevice::wr32(uint32_t off, uint32_t val) {
    *reinterpret_cast<volatile uint32_t*>(m_base + off) = val;
}

uint16_t SdDevice::rd16(uint32_t off) {
    return *reinterpret_cast<volatile uint16_t*>(m_base + off);
}

void SdDevice::wr16(uint32_t off, uint16_t val) {
    *reinterpret_cast<volatile uint16_t*>(m_base + off) = val;
}

uint8_t SdDevice::rd8(uint32_t off) {
    return *(m_base + off);
}

void SdDevice::wr8(uint32_t off, uint8_t val) {
    *(m_base + off) = val;
}

// ==========================================================================
// Controller reset
// ==========================================================================

int SdDevice::reset() {
    wr8(reg::SW_RESET, RST_ALL);

    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        if ((rd8(reg::SW_RESET) & RST_ALL) == 0)
            return 0;
    }

    cprintf("sdhci: reset timeout\n");
    return -1;
}

// ==========================================================================
// Clock setup — use the slowest initial divider for card identification
// ==========================================================================

int SdDevice::clock_setup() {
    // Disable clock first
    wr16(reg::CLOCK_CTRL, 0);

    // Use divider 128 (0x40 in upper byte → actual divider = 2 * 0x40 = 128)
    // This gives roughly 400 kHz from a typical 50 MHz base clock
    uint16_t clk = (0x40 << 8) | CLK_INT_EN;
    wr16(reg::CLOCK_CTRL, clk);

    // Wait for internal clock stable
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        if (rd16(reg::CLOCK_CTRL) & CLK_INT_STABLE) {
            // Enable SD clock output
            wr16(reg::CLOCK_CTRL, rd16(reg::CLOCK_CTRL) | CLK_SD_EN);
            return 0;
        }
    }

    cprintf("sdhci: clock not stable\n");
    return -1;
}

// ==========================================================================
// Power on — 3.3V
// ==========================================================================

int SdDevice::power_on() {
    wr8(reg::POWER_CTRL, PWR_3V3 | PWR_ON);

    // Set data timeout to maximum
    wr8(reg::TIMEOUT_CTRL, 0x0E);

    // Enable all normal + error interrupt status bits
    wr16(reg::INT_ENABLE, 0xFFFF);
    wr16(reg::ERR_ENABLE, 0xFFFF);

    // Disable interrupt signals (we poll)
    wr16(reg::INT_SIGNAL, 0);
    wr16(reg::ERR_SIGNAL, 0);

    return 0;
}

// ==========================================================================
// Send a command and wait for completion
// ==========================================================================

int SdDevice::send_cmd(uint8_t index, uint32_t arg, uint16_t flags) {
    // Wait for CMD line to be free
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        if ((rd32(reg::PRESENT_STATE) & PS_CMD_INHIBIT) == 0)
            goto cmd_ready;
    }
    cprintf("sdhci: CMD%d cmd inhibit timeout\n", index);
    return -1;

cmd_ready:
    // If data command, also wait for DAT line
    if (flags & CMD_DATA) {
        for (int i = 0; i < CMD_TIMEOUT_US; i++) {
            if ((rd32(reg::PRESENT_STATE) & PS_DAT_INHIBIT) == 0)
                goto dat_ready;
        }
        cprintf("sdhci: CMD%d dat inhibit timeout\n", index);
        return -1;
    }

dat_ready:
    // Clear any pending interrupt status
    wr16(reg::INT_STATUS, 0xFFFF);
    wr16(reg::ERR_STATUS, 0xFFFF);

    // Set argument
    wr32(reg::ARGUMENT, arg);

    // Issue command (index in bits [13:8], flags in bits [7:0])
    uint16_t cmd_val = (static_cast<uint16_t>(index) << 8) | flags;
    wr16(reg::COMMAND, cmd_val);

    return wait_cmd_done();
}

int SdDevice::wait_cmd_done() {
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = rd16(reg::INT_STATUS);
        if (status & INT_ERROR) {
            uint16_t err = rd16(reg::ERR_STATUS);
            wr16(reg::INT_STATUS, status);
            wr16(reg::ERR_STATUS, err);
            cprintf("sdhci: command error, status=0x%x err=0x%x\n", status, err);
            return -1;
        }
        if (status & INT_CMD_DONE) {
            wr16(reg::INT_STATUS, INT_CMD_DONE);
            return 0;
        }
    }
    cprintf("sdhci: command complete timeout\n");
    return -1;
}

int SdDevice::wait_xfer_done() {
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = rd16(reg::INT_STATUS);
        if (status & INT_ERROR) {
            uint16_t err = rd16(reg::ERR_STATUS);
            wr16(reg::INT_STATUS, status);
            wr16(reg::ERR_STATUS, err);
            cprintf("sdhci: transfer error, status=0x%x err=0x%x\n", status, err);
            return -1;
        }
        if (status & INT_XFER_DONE) {
            wr16(reg::INT_STATUS, INT_XFER_DONE);
            return 0;
        }
    }
    cprintf("sdhci: transfer complete timeout\n");
    return -1;
}

uint32_t SdDevice::read_response(int idx) {
    return rd32(reg::RESPONSE0 + idx * 4);
}

// ==========================================================================
// SD card identification and initialization
// ==========================================================================

int SdDevice::card_identify() {
    // CMD0: GO_IDLE_STATE (no response)
    if (send_cmd(SD_CMD0_GO_IDLE, 0, CMD_RESP_NONE) != 0) {
        cprintf("sdhci: CMD0 failed\n");
        return -1;
    }

    // CMD8: SEND_IF_COND — check SD v2+ (arg: VHS=1, check pattern=0xAA)
    if (send_cmd(SD_CMD8_SEND_IF, 0x1AA, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
        cprintf("sdhci: CMD8 failed (not SD v2 card?)\n");
        return -1;
    }

    uint32_t r7 = read_response(0);
    if ((r7 & 0xFF) != 0xAA) {
        cprintf("sdhci: CMD8 check pattern mismatch: 0x%x\n", r7);
        return -1;
    }

    // ACMD41 loop: send CMD55 + ACMD41 until card is ready
    m_sdhc = false;
    for (int retry = 0; retry < ACMD41_RETRIES; retry++) {
        // CMD55: APP_CMD (next command is application-specific)
        if (send_cmd(SD_CMD55_APP, 0, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
            cprintf("sdhci: CMD55 failed\n");
            return -1;
        }

        // ACMD41: SD_SEND_OP_COND (R3 response — no CRC/Index check)
        if (send_cmd(SD_ACMD41_OP_COND, ACMD41_HCS | ACMD41_VOLTAGE, CMD_RESP_48) != 0) {
            cprintf("sdhci: ACMD41 failed\n");
            return -1;
        }

        uint32_t ocr = read_response(0);
        if (ocr & OCR_BUSY) {
            m_sdhc = (ocr & OCR_CCS) != 0;
            cprintf("sdhci: card ready, %s\n", m_sdhc ? "SDHC/SDXC" : "SDSC");
            goto card_ready;
        }
    }
    cprintf("sdhci: ACMD41 timeout — card not ready\n");
    return -1;

card_ready:
    // CMD2: ALL_SEND_CID (R2 = 136-bit response)
    if (send_cmd(SD_CMD2_ALL_CID, 0, CMD_RESP_136 | CMD_CRC_EN) != 0) {
        cprintf("sdhci: CMD2 failed\n");
        return -1;
    }

    // CMD3: SEND_RELATIVE_ADDR — get RCA (R6 response)
    if (send_cmd(SD_CMD3_SEND_RCA, 0, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
        cprintf("sdhci: CMD3 failed\n");
        return -1;
    }
    m_rca = static_cast<uint16_t>(read_response(0) >> 16);
    cprintf("sdhci: RCA = 0x%x\n", m_rca);

    // CMD7: SELECT_CARD (R1b = 48-bit + busy)
    if (send_cmd(SD_CMD7_SELECT, static_cast<uint32_t>(m_rca) << 16, CMD_RESP_48_BUSY | CMD_CRC_EN | CMD_IDX_EN) != 0) {
        cprintf("sdhci: CMD7 (SELECT) failed\n");
        return -1;
    }

    // CMD16: SET_BLOCKLEN = 512 (for SDSC; harmless for SDHC)
    if (send_cmd(SD_CMD16_SET_BLKLEN, 512, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
        cprintf("sdhci: CMD16 failed\n");
        return -1;
    }

    return 0;
}

// ==========================================================================
// Single-block read (CMD17)
// ==========================================================================

int SdDevice::read_single(uint32_t lba, void* buf) {
    uint32_t addr = m_sdhc ? lba : lba * 512;

    // Set block size = 512, block count = 1
    wr16(reg::BLOCK_SIZE, 512);
    wr16(reg::BLOCK_COUNT, 1);

    // Transfer mode: single block, read
    wr16(reg::XFER_MODE, XFER_READ);

    // Send CMD17 with data flag
    if (send_cmd(SD_CMD17_READ, addr, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN | CMD_DATA) != 0)
        return -1;

    // Wait for buffer read ready
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = rd16(reg::INT_STATUS);
        if (status & INT_ERROR) {
            cprintf("sdhci: read data error\n");
            return -1;
        }
        if (status & INT_BUF_RD_READY) {
            wr16(reg::INT_STATUS, INT_BUF_RD_READY);
            break;
        }
        if (i == CMD_TIMEOUT_US - 1) {
            cprintf("sdhci: read buffer ready timeout\n");
            return -1;
        }
    }

    // Read 128 dwords (512 bytes) from buffer data port
    auto* dst = static_cast<uint32_t*>(buf);
    for (int i = 0; i < 128; i++) {
        dst[i] = rd32(reg::BUF_DATA);
    }

    return wait_xfer_done();
}

// ==========================================================================
// Single-block write (CMD24)
// ==========================================================================

int SdDevice::write_single(uint32_t lba, const void* buf) {
    uint32_t addr = m_sdhc ? lba : lba * 512;

    // Set block size = 512, block count = 1
    wr16(reg::BLOCK_SIZE, 512);
    wr16(reg::BLOCK_COUNT, 1);

    // Transfer mode: single block, write (direction bit = 0)
    wr16(reg::XFER_MODE, 0);

    // Send CMD24 with data flag
    if (send_cmd(SD_CMD24_WRITE, addr, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN | CMD_DATA) != 0)
        return -1;

    // Wait for buffer write ready
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = rd16(reg::INT_STATUS);
        if (status & INT_ERROR) {
            cprintf("sdhci: write data error\n");
            return -1;
        }
        if (status & INT_BUF_WR_READY) {
            wr16(reg::INT_STATUS, INT_BUF_WR_READY);
            break;
        }
        if (i == CMD_TIMEOUT_US - 1) {
            cprintf("sdhci: write buffer ready timeout\n");
            return -1;
        }
    }

    // Write 128 dwords (512 bytes) to buffer data port
    auto const* src = static_cast<const uint32_t*>(buf);
    for (int i = 0; i < 128; i++) {
        wr32(reg::BUF_DATA, src[i]);
    }

    return wait_xfer_done();
}

// ==========================================================================
// BlockDevice interface implementation
// ==========================================================================

int SdDevice::init(volatile uint8_t* base) {
    m_base = base;

    // Read controller version
    uint16_t ver = rd16(reg::HOST_VERSION);
    cprintf("sdhci: controller version %d.%02d\n", (ver >> 8) + 1, ver & 0xFF);

    if (reset() != 0)
        return -1;
    if (clock_setup() != 0)
        return -1;
    if (power_on() != 0)
        return -1;
    if (card_identify() != 0)
        return -1;

    // Set device metadata
    type = blk::DeviceType::Disk;
    strncpy(name, "sd0", sizeof(name));

    // Read card capacity via CMD9 would be proper, but for simplicity
    // report a reasonable default. QEMU's SD emulation typically reports
    // the image file size. We can refine this later with CMD9 (SEND_CSD).
    // For now, use the image size hint from QEMU (64 MB = 131072 sectors).
    size = 131072;

    cprintf("sdhci: SD card initialized as '%s'\n", name);
    return 0;
}

int SdDevice::read(uint32_t block_number, void* buf, size_t block_count) {
    auto* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < block_count; i++) {
        if (read_single(block_number + i, p + i * 512) != 0)
            return -1;
    }
    return 0;
}

int SdDevice::write(uint32_t block_number, const void* buf, size_t block_count) {
    auto const* p = static_cast<const uint8_t*>(buf);
    for (size_t i = 0; i < block_count; i++) {
        if (write_single(block_number + i, p + i * 512) != 0)
            return -1;
    }
    return 0;
}

void SdDevice::print_info() {
    cprintf("SD Card '%s': %s, RCA=0x%x, %d sectors (%d MB)\n", name, m_sdhc ? "SDHC" : "SDSC", m_rca, size,
            size / 2048);
}

void SdDevice::test() {
    cprintf("sdhci: read test — reading sector 0\n");

    uint8_t buf[512];
    if (read(0, buf, 1) != 0) {
        cprintf("sdhci: read test FAILED\n");
        return;
    }

    // Print first 16 bytes
    cprintf("sdhci: sector 0 data: ");
    for (int i = 0; i < 16; i++)
        cprintf("%02x ", buf[i]);
    cprintf("...\n");
    cprintf("sdhci: read test PASSED\n");
}

// ==========================================================================
// Module init — find SDHCI via PCI and initialize
// ==========================================================================

namespace sdhci {

static bool s_initialized = false;

int init() {
    int bus, dev, func;

    // Try SDHCI DMA interface first (prog-if 0x01), then base (0x00)
    bool found = pci::find_by_class(PCI_CLASS_SYSTEM, PCI_SUBCLASS_SD, 0x01, &bus, &dev, &func);
    if (!found) {
        found = pci::find_by_class(PCI_CLASS_SYSTEM, PCI_SUBCLASS_SD, 0x00, &bus, &dev, &func);
    }
    if (!found) {
        cprintf("sdhci: no SDHCI controller found on PCI bus\n");
        return -1;
    }

    uint32_t id = pci::config_read32(bus, dev, func, pci::VENDOR_ID);
    cprintf("sdhci: found controller at PCI %d:%d.%d [%04x:%04x]\n", bus, dev, func, static_cast<unsigned>(id & 0xFFFF),
            static_cast<unsigned>(id >> 16));

    // Enable bus master + memory space
    pci::enable_bus_master(bus, dev, func);

    // Read BAR0 (SDHCI MMIO registers)
    uint32_t bar0 = pci::read_bar(bus, dev, func, 0);
    if (bar0 == 0 || (bar0 & 1)) {
        cprintf("sdhci: invalid BAR0 = 0x%x\n", bar0);
        return -1;
    }

    uintptr_t phys = bar0 & 0xFFFFF000u;        // mask low bits
    constexpr size_t SDHCI_MMIO_SIZE = 0x1000;  // 4 KB (one page, covers all regs)

    uintptr_t va = vmm::mmio_map(phys, SDHCI_MMIO_SIZE, VM_WRITE | VM_NOCACHE);
    if (va == 0) {
        cprintf("sdhci: failed to map BAR0 at 0x%lx\n", static_cast<unsigned long>(phys));
        return -1;
    }

    cprintf("sdhci: MMIO at PA 0x%lx -> VA 0x%lx\n", static_cast<unsigned long>(phys), static_cast<unsigned long>(va));

    if (s_sd_device.init(reinterpret_cast<volatile uint8_t*>(va)) != 0) {
        cprintf("sdhci: device init failed\n");
        return -1;
    }

    s_initialized = true;
    return 0;
}

SdDevice* get_device() {
    return s_initialized ? &s_sd_device : nullptr;
}

}  // namespace sdhci
