#include "sdhci.h"
#include "drivers/mmio.h"
#include "drivers/pci.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include <asm/page.h>

namespace reg {

constexpr uint32_t BLOCK_SIZE = 0x04;  // lower 16: block size; upper 16: block count
constexpr uint32_t BLOCK_COUNT = 0x06;
constexpr uint32_t ARGUMENT = 0x08;
constexpr uint32_t XFER_MODE = 0x0C;
constexpr uint32_t COMMAND = 0x0E;
constexpr uint32_t RESPONSE0 = 0x10;
constexpr uint32_t BUF_DATA = 0x20;
constexpr uint32_t PRESENT_STATE = 0x24;
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
constexpr uint32_t HOST_VERSION = 0xFE;

}  // namespace reg

constexpr uint32_t PS_CMD_INHIBIT = (1U << 0);
constexpr uint32_t PS_DAT_INHIBIT = (1U << 1);

constexpr uint16_t INT_CMD_DONE = (1U << 0);
constexpr uint16_t INT_XFER_DONE = (1U << 1);
constexpr uint16_t INT_BUF_WR_READY = (1U << 4);
constexpr uint16_t INT_BUF_RD_READY = (1U << 5);
constexpr uint16_t INT_ERROR = (1U << 15);

constexpr uint16_t CLK_INT_EN = (1U << 0);
constexpr uint16_t CLK_INT_STABLE = (1U << 1);
constexpr uint16_t CLK_SD_EN = (1U << 2);

constexpr uint8_t RST_ALL = (1U << 0);

constexpr uint8_t PWR_ON = (1U << 0);
constexpr uint8_t PWR_3V3 = (7U << 1);  // 3.3V

constexpr uint16_t CMD_RESP_NONE = 0x00;
constexpr uint16_t CMD_RESP_136 = 0x01;      // R2
constexpr uint16_t CMD_RESP_48 = 0x02;       // R1, R3, R6, R7
constexpr uint16_t CMD_RESP_48_BUSY = 0x03;  // R1b
constexpr uint16_t CMD_CRC_EN = 0x08;
constexpr uint16_t CMD_IDX_EN = 0x10;
constexpr uint16_t CMD_DATA = 0x20;

constexpr uint16_t XFER_READ = (1U << 4);

constexpr uint8_t SD_CMD0_GO_IDLE = 0;
constexpr uint8_t SD_CMD2_ALL_CID = 2;
constexpr uint8_t SD_CMD3_SEND_RCA = 3;
constexpr uint8_t SD_CMD7_SELECT = 7;
constexpr uint8_t SD_CMD8_SEND_IF = 8;
constexpr uint8_t SD_CMD9_SEND_CSD = 9;
constexpr uint8_t SD_CMD16_SET_BLKLEN = 16;
constexpr uint8_t SD_CMD17_READ = 17;
constexpr uint8_t SD_CMD24_WRITE = 24;
constexpr uint8_t SD_CMD55_APP = 55;
constexpr uint8_t SD_ACMD41_OP_COND = 41;

constexpr uint32_t ACMD41_HCS = (1U << 30);      // Host Capacity Support (SDHC)
constexpr uint32_t ACMD41_VOLTAGE = 0x00FF8000;  // 2.7-3.6V

constexpr uint32_t OCR_BUSY = (1U << 31);  // Card power-up status (ready)
constexpr uint32_t OCR_CCS = (1U << 30);   // Card Capacity Status (SDHC)

constexpr int CMD_TIMEOUT_US = 1000000;  // 1 second in busy-loop iterations
constexpr int ACMD41_RETRIES = 100;

int SdDevice::reset() {
    mmio::write8(base_, reg::SW_RESET, RST_ALL);

    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        if ((mmio::read8(base_, reg::SW_RESET) & RST_ALL) == 0)
            return 0;
    }

    cprintf("sdhci: reset timeout\n");
    return -1;
}

int SdDevice::clock_setup() {
    // Disable clock first
    mmio::write16(base_, reg::CLOCK_CTRL, 0);

    // Use divider 128 (0x40 in upper byte → actual divider = 2 * 0x40 = 128)
    // This gives roughly 400 kHz from a typical 50 MHz base clock
    uint16_t clk = (0x40 << 8) | CLK_INT_EN;
    mmio::write16(base_, reg::CLOCK_CTRL, clk);

    // Wait for internal clock stable
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        if (mmio::read16(base_, reg::CLOCK_CTRL) & CLK_INT_STABLE) {
            // Enable SD clock output
            mmio::write16(base_, reg::CLOCK_CTRL, mmio::read16(base_, reg::CLOCK_CTRL) | CLK_SD_EN);
            return 0;
        }
    }

    cprintf("sdhci: clock not stable\n");
    return -1;
}

int SdDevice::power_on() {
    mmio::write8(base_, reg::POWER_CTRL, PWR_3V3 | PWR_ON);

    mmio::write8(base_, reg::TIMEOUT_CTRL, 0x0E);

    // Enable all normal + error interrupt status bits
    mmio::write16(base_, reg::INT_ENABLE, 0xFFFF);
    mmio::write16(base_, reg::ERR_ENABLE, 0xFFFF);

    // Disable interrupt signals (we poll)
    mmio::write16(base_, reg::INT_SIGNAL, 0);
    mmio::write16(base_, reg::ERR_SIGNAL, 0);

    return 0;
}

int SdDevice::send_cmd(uint8_t index, uint32_t arg, uint16_t flags) {
    int wait_time{};

    for (wait_time = 0; wait_time < CMD_TIMEOUT_US; wait_time++) {
        if ((mmio::read32(base_, reg::PRESENT_STATE) & PS_CMD_INHIBIT) == 0)
            break;
    }
    if (wait_time == CMD_TIMEOUT_US) {
        cprintf("sdhci: CMD%d cmd inhibit timeout\n", index);
        return -1;
    }

    if (flags & CMD_DATA) {
        for (wait_time = 0; wait_time < CMD_TIMEOUT_US; wait_time++) {
            if ((mmio::read32(base_, reg::PRESENT_STATE) & PS_DAT_INHIBIT) == 0)
                break;
        }
        if (wait_time == CMD_TIMEOUT_US) {
            cprintf("sdhci: CMD%d dat inhibit timeout\n", index);
            return -1;
        }
    }

    mmio::write16(base_, reg::INT_STATUS, 0xFFFF);
    mmio::write16(base_, reg::ERR_STATUS, 0xFFFF);

    mmio::write32(base_, reg::ARGUMENT, arg);

    uint16_t cmd_val = (static_cast<uint16_t>(index) << 8) | flags;
    mmio::write16(base_, reg::COMMAND, cmd_val);

    return wait_cmd_done();
}

int SdDevice::wait_cmd_done() {
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            uint16_t err = mmio::read16(base_, reg::ERR_STATUS);
            mmio::write16(base_, reg::INT_STATUS, status);
            mmio::write16(base_, reg::ERR_STATUS, err);
            cprintf("sdhci: command error, status=0x%x err=0x%x\n", status, err);
            return -1;
        }
        if (status & INT_CMD_DONE) {
            mmio::write16(base_, reg::INT_STATUS, INT_CMD_DONE);
            return 0;
        }
    }

    cprintf("sdhci: command complete timeout\n");
    return -1;
}

int SdDevice::wait_xfer_done() {
    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            uint16_t err = mmio::read16(base_, reg::ERR_STATUS);
            mmio::write16(base_, reg::INT_STATUS, status);
            mmio::write16(base_, reg::ERR_STATUS, err);
            cprintf("sdhci: transfer error, status=0x%x err=0x%x\n", status, err);
            return -1;
        }
        if (status & INT_XFER_DONE) {
            mmio::write16(base_, reg::INT_STATUS, INT_XFER_DONE);
            return 0;
        }
    }

    cprintf("sdhci: transfer complete timeout\n");
    return -1;
}

uint32_t SdDevice::read_response(int idx) {
    return mmio::read32(base_, reg::RESPONSE0 + idx * 4);
}

int SdDevice::read_csd() {
    // CMD9: SEND_CSD — must be called while card is in Stand-by State (after CMD3, before CMD7).
    // R2 response (136-bit): SDHCI strips bits[7:0] (CRC+end) and stores CSD[127:8] in
    // RESPONSE[119:0], so registers map as:
    //   RESPONSE3[23:0] = CSD[127:104],  RESPONSE3[31:24] = 0
    //   RESPONSE2[31:0] = CSD[103:72]
    //   RESPONSE1[31:0] = CSD[71:40]
    //   RESPONSE0[31:0] = CSD[39:8]
    if (send_cmd(SD_CMD9_SEND_CSD, static_cast<uint32_t>(rca_) << 16, CMD_RESP_136 | CMD_CRC_EN) != 0) {
        cprintf("sdhci: CMD9 failed\n");
        return -1;
    }

    uint32_t r3 = read_response(3);
    uint32_t r2 = read_response(2);
    uint32_t r1 = read_response(1);

    uint32_t csd_structure = (r3 >> 22) & 0x3;  // CSD[127:126]

    if (csd_structure == 1) {
        // CSD v2 (SDHC/SDXC): capacity = (C_SIZE + 1) * 512 KB
        // C_SIZE [69:48] = RESPONSE1[29:8]
        uint32_t c_size = (r1 >> 8) & 0x3FFFFF;
        size = (c_size + 1) * 1024;
        cprintf("sdhci: CSD v2, C_SIZE=%u -> %u sectors (%u MB)\n", c_size, size, size / 2048);
    } else if (csd_structure == 0) {
        // CSD v1 (SDSC): capacity = BLOCKNR * BLOCK_LEN
        // READ_BL_LEN [83:80] = RESPONSE2[11:8]
        uint32_t read_bl_len = (r2 >> 8) & 0xF;
        // C_SIZE [73:62]: top 2 bits in RESPONSE2[1:0], bottom 10 bits in RESPONSE1[31:22]
        uint32_t c_size = ((r2 & 0x3) << 10) | (r1 >> 22);
        // C_SIZE_MULT [49:47] = RESPONSE1[9:7]
        uint32_t c_size_mult = (r1 >> 7) & 0x7;
        uint32_t blocknr = (c_size + 1) << (c_size_mult + 2);
        size = (blocknr * (1U << read_bl_len)) / 512;
        cprintf("sdhci: CSD v1, C_SIZE=%u C_SIZE_MULT=%u READ_BL_LEN=%u -> %u sectors (%u MB)\n", c_size, c_size_mult,
                read_bl_len, size, size / 2048);
    } else {
        cprintf("sdhci: unknown CSD structure %u, defaulting to 131072 sectors\n", csd_structure);
        size = 131072;
    }

    return 0;
}

int SdDevice::card_identify() {
    if (send_cmd(SD_CMD0_GO_IDLE, 0, CMD_RESP_NONE) != 0) {
        cprintf("sdhci: CMD0 failed\n");
        return -1;
    }

    if (send_cmd(SD_CMD8_SEND_IF, 0x1AA, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
        cprintf("sdhci: CMD8 failed (not SD v2 card?)\n");
        return -1;
    }

    uint32_t r7 = read_response(0);
    if ((r7 & 0xFF) != 0xAA) {
        cprintf("sdhci: CMD8 check pattern mismatch: 0x%x\n", r7);
        return -1;
    }

    sdhc_ = false;

    int retry{};
    for (retry = 0; retry < ACMD41_RETRIES; retry++) {
        if (send_cmd(SD_CMD55_APP, 0, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
            cprintf("sdhci: CMD55 failed\n");
            return -1;
        }

        if (send_cmd(SD_ACMD41_OP_COND, ACMD41_HCS | ACMD41_VOLTAGE, CMD_RESP_48) != 0) {
            cprintf("sdhci: ACMD41 failed\n");
            return -1;
        }

        uint32_t ocr = read_response(0);
        if (ocr & OCR_BUSY) {
            sdhc_ = (ocr & OCR_CCS) != 0;
            cprintf("sdhci: card ready, %s\n", sdhc_ ? "SDHC/SDXC" : "SDSC");
            break;
        }
    }

    if (retry == ACMD41_RETRIES) {
        cprintf("sdhci: ACMD41 timeout — card not ready\n");
        return -1;
    }

    if (send_cmd(SD_CMD2_ALL_CID, 0, CMD_RESP_136 | CMD_CRC_EN) != 0) {
        cprintf("sdhci: CMD2 failed\n");
        return -1;
    }

    if (send_cmd(SD_CMD3_SEND_RCA, 0, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN) != 0) {
        cprintf("sdhci: CMD3 failed\n");
        return -1;
    }

    rca_ = static_cast<uint16_t>(read_response(0) >> 16);
    cprintf("sdhci: RCA = 0x%x\n", rca_);

    if (read_csd() != 0)
        return -1;

    if (send_cmd(SD_CMD7_SELECT, static_cast<uint32_t>(rca_) << 16, CMD_RESP_48_BUSY | CMD_CRC_EN | CMD_IDX_EN) != 0) {
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

int SdDevice::read_single(uint32_t lba, void* buf) {
    uint32_t addr = sdhc_ ? lba : lba * 512;

    mmio::write16(base_, reg::BLOCK_SIZE, 512);
    mmio::write16(base_, reg::BLOCK_COUNT, 1);

    mmio::write16(base_, reg::XFER_MODE, XFER_READ);

    if (send_cmd(SD_CMD17_READ, addr, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN | CMD_DATA) != 0)
        return -1;

    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            cprintf("sdhci: read data error\n");
            return -1;
        }
        if (status & INT_BUF_RD_READY) {
            mmio::write16(base_, reg::INT_STATUS, INT_BUF_RD_READY);
            break;
        }
        if (i == CMD_TIMEOUT_US - 1) {
            cprintf("sdhci: read buffer ready timeout\n");
            return -1;
        }
    }

    auto* dst = static_cast<uint32_t*>(buf);
    for (int i = 0; i < 128; i++) {
        dst[i] = mmio::read32(base_, reg::BUF_DATA);
    }

    return wait_xfer_done();
}

int SdDevice::write_single(uint32_t lba, const void* buf) {
    uint32_t addr = sdhc_ ? lba : lba * 512;

    mmio::write16(base_, reg::BLOCK_SIZE, 512);
    mmio::write16(base_, reg::BLOCK_COUNT, 1);

    mmio::write16(base_, reg::XFER_MODE, 0);

    if (send_cmd(SD_CMD24_WRITE, addr, CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN | CMD_DATA) != 0)
        return -1;

    for (int i = 0; i < CMD_TIMEOUT_US; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            cprintf("sdhci: write data error\n");
            return -1;
        }
        if (status & INT_BUF_WR_READY) {
            mmio::write16(base_, reg::INT_STATUS, INT_BUF_WR_READY);
            break;
        }
        if (i == CMD_TIMEOUT_US - 1) {
            cprintf("sdhci: write buffer ready timeout\n");
            return -1;
        }
    }

    auto const* src = static_cast<const uint32_t*>(buf);
    for (int i = 0; i < 128; i++) {
        mmio::write32(base_, reg::BUF_DATA, src[i]);
    }

    return wait_xfer_done();
}

int SdDevice::init(volatile uint8_t* base, int index) {
    base_ = base;

    uint16_t ver = mmio::read16(base_, reg::HOST_VERSION);
    cprintf("sdhci: controller version %d.%02d\n", (ver >> 8) + 1, ver & 0xFF);

    if (reset() != 0)
        return -1;
    if (clock_setup() != 0)
        return -1;
    if (power_on() != 0)
        return -1;
    if (card_identify() != 0)
        return -1;

    type = blk::DeviceType::Disk;
    name[0] = 's';
    name[1] = 'd';
    name[2] = static_cast<char>('0' + index);
    name[3] = '\0';

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
    cprintf("SD Card '%s': %s, RCA=0x%x, %d sectors (%d MB)\n", name, sdhc_ ? "SDHC" : "SDSC", rca_, size, size / 2048);
}

namespace sdhci {

namespace {

const pci::DriverId SDHCI_IDS[] = {
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_SYSTEM_PERIPHERAL, pci::SUBCLASS_SD_HOST, pci::INTERFACE_SDHCI},
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_SYSTEM_PERIPHERAL, pci::SUBCLASS_SD_HOST, pci::INTERFACE_SDHCI_DMA},
};

static int probe_one_controller(SdDevice* dev, int device_index, int bus, int dev_id, int func) {
    uint32_t id = pci::config_read32(bus, dev_id, func, pci::VENDOR_ID);
    cprintf("sdhci: found controller at PCI %d:%d.%d [%04x:%04x]\n", bus, dev_id, func,
            static_cast<unsigned>(id & 0xFFFF), static_cast<unsigned>(id >> 16));

    pci::enable_bus_master(bus, dev_id, func);

    uint32_t bar0 = pci::read_bar(bus, dev_id, func, 0);
    if (bar0 == 0 || (bar0 & 1)) {
        cprintf("sdhci: invalid BAR0 for %d:%d.%d = 0x%x\n", bus, dev_id, func, bar0);
        return -1;
    }

    uintptr_t phys = bar0 & 0xFFFFF000U;
    constexpr size_t SDHCI_MMIO_SIZE = 0x1000;

    uintptr_t va = vmm::mmio_map(phys, SDHCI_MMIO_SIZE, VM_WRITE | VM_NOCACHE);
    if (va == 0) {
        cprintf("sdhci: failed to map BAR0 for %d:%d.%d at 0x%lx\n", bus, dev_id, func,
                static_cast<unsigned long>(phys));
        return -1;
    }

    cprintf("sdhci: MMIO %d:%d.%d at PA 0x%lx -> VA 0x%lx\n", bus, dev_id, func, static_cast<unsigned long>(phys),
            static_cast<unsigned long>(va));

    if (dev->init(reinterpret_cast<volatile uint8_t*>(va), device_index) != 0) {
        cprintf("sdhci: controller %d:%d.%d init failed\n", bus, dev_id, func);
        return -1;
    }

    blk::register_device(dev);
    cprintf("blk: registered SD card '%s' (%d sectors)\n", dev->name, dev->size);

    return 0;
}

const pci::Driver SDHCI_DRIVER = {
    "sdhci",
    SDHCI_IDS,
    static_cast<int>(array_size(SDHCI_IDS)),
    Manager::probe_callback,
};

}  // namespace

int Manager::init() {
    if (s_initialized) {
        return 0;
    }

    if (pci::register_driver(&SDHCI_DRIVER) != 0) {
        cprintf("sdhci: failed to register PCI driver\n");
        return -1;
    }

    s_initialized = true;
    return 0;
}

int Manager::device_count() {
    return static_cast<int>(s_devices.size());
}

SdDevice* Manager::get_device(int index) {
    if (index < 0 || static_cast<size_t>(index) >= s_devices.size()) {
        return nullptr;
    }
    return &s_devices[index];
}

int Manager::probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*) {
    if (s_devices.full()) {
        cprintf("sdhci: too many controllers, max=%d\n", MAX_DEVICES);
        return -1;
    }

    int index = static_cast<int>(s_devices.size());
    int rc = probe_one_controller(&s_devices[index], index, pdev->bus, pdev->dev, pdev->func);
    if (rc != 0) {
        return rc;
    }

    s_devices.commit_back();
    return 0;
}

int init() {
    return Manager::init();
}

int device_count() {
    return Manager::device_count();
}

SdDevice* get_device() {
    return Manager::get_device(0);
}

SdDevice* get_device(int index) {
    return Manager::get_device(index);
}

}  // namespace sdhci
