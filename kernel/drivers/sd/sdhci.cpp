#include <sys/iterator.hpp>
#include <kernel/log.h>
#include "drivers/sd/sdhci.h"
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

constexpr int COMMAND_POLL_LIMIT = 1000000;  // Poll budget; not a wall-clock duration

Error SdhciHost::reset() {
    mmio::write8(base_, reg::SW_RESET, RST_ALL);

    for (int i = 0; i < COMMAND_POLL_LIMIT; i++) {
        if ((mmio::read8(base_, reg::SW_RESET) & RST_ALL) == 0)
            return Error::None;
    }

    klog::error("sdhci: reset timeout\n");
    return Error::Timeout;
}

Error SdhciHost::clock_setup() {
    // Disable clock first
    mmio::write16(base_, reg::CLOCK_CTRL, 0);

    // Use divider 128 (0x40 in upper byte → actual divider = 2 * 0x40 = 128)
    // This gives roughly 400 kHz from a typical 50 MHz base clock
    uint16_t clk = (0x40 << 8) | CLK_INT_EN;
    mmio::write16(base_, reg::CLOCK_CTRL, clk);

    // Wait for internal clock stable
    for (int i = 0; i < COMMAND_POLL_LIMIT; i++) {
        if (mmio::read16(base_, reg::CLOCK_CTRL) & CLK_INT_STABLE) {
            // Enable SD clock output
            mmio::write16(base_, reg::CLOCK_CTRL, mmio::read16(base_, reg::CLOCK_CTRL) | CLK_SD_EN);
            return Error::None;
        }
    }

    klog::error("sdhci: clock not stable\n");
    return Error::Timeout;
}

Error SdhciHost::power_on() {
    mmio::write8(base_, reg::POWER_CTRL, PWR_3V3 | PWR_ON);

    mmio::write8(base_, reg::TIMEOUT_CTRL, 0x0E);

    // Enable all normal + error interrupt status bits
    mmio::write16(base_, reg::INT_ENABLE, 0xFFFF);
    mmio::write16(base_, reg::ERR_ENABLE, 0xFFFF);

    // Disable interrupt signals (we poll)
    mmio::write16(base_, reg::INT_SIGNAL, 0);
    mmio::write16(base_, reg::ERR_SIGNAL, 0);

    return Error::None;
}

Error SdhciHost::send_cmd(uint8_t index, uint32_t arg, uint16_t flags) {
    int wait_time{};

    for (wait_time = 0; wait_time < COMMAND_POLL_LIMIT; wait_time++) {
        if ((mmio::read32(base_, reg::PRESENT_STATE) & PS_CMD_INHIBIT) == 0)
            break;
    }
    if (wait_time == COMMAND_POLL_LIMIT) {
        klog::error("sdhci: CMD%d cmd inhibit timeout\n", index);
        return Error::Timeout;
    }

    if (flags & CMD_DATA) {
        for (wait_time = 0; wait_time < COMMAND_POLL_LIMIT; wait_time++) {
            if ((mmio::read32(base_, reg::PRESENT_STATE) & PS_DAT_INHIBIT) == 0)
                break;
        }
        if (wait_time == COMMAND_POLL_LIMIT) {
            klog::error("sdhci: CMD%d dat inhibit timeout\n", index);
            return Error::Timeout;
        }
    }

    mmio::write16(base_, reg::INT_STATUS, 0xFFFF);
    mmio::write16(base_, reg::ERR_STATUS, 0xFFFF);

    mmio::write32(base_, reg::ARGUMENT, arg);

    uint16_t cmd_val = (static_cast<uint16_t>(index) << 8) | flags;
    mmio::write16(base_, reg::COMMAND, cmd_val);

    return wait_cmd_done();
}

Error SdhciHost::wait_cmd_done() {
    for (int i = 0; i < COMMAND_POLL_LIMIT; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            uint16_t err = mmio::read16(base_, reg::ERR_STATUS);
            mmio::write16(base_, reg::INT_STATUS, status);
            mmio::write16(base_, reg::ERR_STATUS, err);
            klog::error("sdhci: command error, status=0x%x err=0x%x\n", status, err);
            return Error::Io;
        }
        if (status & INT_CMD_DONE) {
            mmio::write16(base_, reg::INT_STATUS, INT_CMD_DONE);
            return Error::None;
        }
    }

    klog::error("sdhci: command complete timeout\n");
    return Error::Timeout;
}

Error SdhciHost::wait_xfer_done() {
    for (int i = 0; i < COMMAND_POLL_LIMIT; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            uint16_t err = mmio::read16(base_, reg::ERR_STATUS);
            mmio::write16(base_, reg::INT_STATUS, status);
            mmio::write16(base_, reg::ERR_STATUS, err);
            klog::error("sdhci: transfer error, status=0x%x err=0x%x\n", status, err);
            return Error::Io;
        }
        if (status & INT_XFER_DONE) {
            mmio::write16(base_, reg::INT_STATUS, INT_XFER_DONE);
            return Error::None;
        }
    }

    klog::error("sdhci: transfer complete timeout\n");
    return Error::Timeout;
}

uint32_t SdhciHost::read_response(int index) {
    return mmio::read32(base_, reg::RESPONSE0 + index * 4);
}

Error SdhciHost::wait_buffer(uint16_t ready) {
    for (int i = 0; i < COMMAND_POLL_LIMIT; i++) {
        uint16_t status = mmio::read16(base_, reg::INT_STATUS);
        if (status & INT_ERROR) {
            klog::error("sdhci: %s data error\n", ready == INT_BUF_RD_READY ? "read" : "write");
            return Error::Io;
        }
        if (status & ready) {
            mmio::write16(base_, reg::INT_STATUS, ready);
            return Error::None;
        }
    }
    klog::error("sdhci: %s buffer ready timeout\n", ready == INT_BUF_RD_READY ? "read" : "write");
    return Error::Timeout;
}

Error SdhciHost::start() {
    uint16_t ver = mmio::read16(base_, reg::HOST_VERSION);
    klog::debug("sdhci: controller version %d.%02d\n", (ver >> 8) + 1, ver & 0xFF);
    TRY(reset());
    TRY(clock_setup());
    return power_on();
}

void SdhciHost::stop() {
    if (!base_) {
        return;
    }
    // This driver uses PIO, never DMA. Disable signals and remove card power
    // before its register mapping is released.
    mmio::write32(base_, reg::INT_SIGNAL, 0);
    mmio::write32(base_, reg::INT_ENABLE, 0);
    mmio::write16(base_, reg::CLOCK_CTRL, 0);
    mmio::write8(base_, reg::POWER_CTRL, 0);
    base_ = nullptr;
}

Error SdhciHost::execute(SdCommand& command) {
    uint16_t flags = CMD_RESP_NONE;
    switch (command.response) {
        case SdResponseType::None: flags = CMD_RESP_NONE; break;
        case SdResponseType::R1:
        case SdResponseType::R6:
        case SdResponseType::R7: flags = CMD_RESP_48 | CMD_CRC_EN | CMD_IDX_EN; break;
        case SdResponseType::R1b: flags = CMD_RESP_48_BUSY | CMD_CRC_EN | CMD_IDX_EN; break;
        case SdResponseType::R2: flags = CMD_RESP_136 | CMD_CRC_EN; break;
        case SdResponseType::R3: flags = CMD_RESP_48; break;
    }
    if (command.data) {
        mmio::write16(base_, reg::BLOCK_SIZE, SdCommand::BLOCK_BYTES);
        mmio::write16(base_, reg::BLOCK_COUNT, 1);
        mmio::write16(base_, reg::XFER_MODE, command.write ? 0 : XFER_READ);
        flags |= CMD_DATA;
    }
    TRY(send_cmd(command.index, command.argument, flags));

    if (command.response == SdResponseType::R2) {
        // The controller drops the CRC byte and stores R2 bits [127:8] in
        // RESPONSE[119:0]; restore the bit positions SdCommand defines.
        uint32_t r0 = read_response(0);
        uint32_t r1 = read_response(1);
        uint32_t r2 = read_response(2);
        uint32_t r3 = read_response(3);
        command.reply[0] = (r3 << 8) | (r2 >> 24);
        command.reply[1] = (r2 << 8) | (r1 >> 24);
        command.reply[2] = (r1 << 8) | (r0 >> 24);
        command.reply[3] = r0 << 8;
    } else if (command.response != SdResponseType::None) {
        command.reply[0] = read_response(0);
    }
    if (!command.data) {
        return Error::None;
    }

    TRY(wait_buffer(command.write ? INT_BUF_WR_READY : INT_BUF_RD_READY));
    auto* bytes = static_cast<uint8_t*>(command.data);
    for (size_t i = 0; i < SdCommand::BLOCK_BYTES / sizeof(uint32_t); i++) {
        uint32_t word{};
        if (command.write) {
            memcpy(&word, bytes + i * sizeof(word), sizeof(word));
            mmio::write32(base_, reg::BUF_DATA, word);
        } else {
            word = mmio::read32(base_, reg::BUF_DATA);
            memcpy(bytes + i * sizeof(word), &word, sizeof(word));
        }
    }
    return wait_xfer_done();
}

Error SdhciDevice::init(volatile uint8_t* base, int index) {
    ENSURE(base, Error::Invalid);
    return start("sd", index, [this, base] { host_.attach(base); });
}

namespace sdhci {

namespace {

const pci::DriverId SDHCI_IDS[] = {
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_SYSTEM_PERIPHERAL, pci::SUBCLASS_SD_HOST, pci::INTERFACE_SDHCI},
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_SYSTEM_PERIPHERAL, pci::SUBCLASS_SD_HOST, pci::INTERFACE_SDHCI_DMA},
};

const pci::Driver SDHCI_DRIVER = {
    "sdhci",
    SDHCI_IDS,
    static_cast<int>(sys::size(SDHCI_IDS)),
    Manager::probe_callback,
};

}  // namespace

int Manager::init() {
    if (initialized_) {
        return 0;
    }

    Error registered = pci::register_driver(&SDHCI_DRIVER);
    if (registered != Error::None) {
        return static_cast<int>(registered);
    }

    initialized_ = true;
    return 0;
}

int Manager::device_count() {
    return static_cast<int>(device_count_);
}

SdhciDevice* Manager::find_device(int index) {
    if (index < 0 || static_cast<size_t>(index) >= device_count_) {
        return nullptr;
    }
    return devices_[index].state() == blk::DeviceState::Ready ? &devices_[index] : nullptr;
}

Error Manager::probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*) {
    ENSURE(pdev, Error::Invalid);
    sys::lock_guard<Mutex> guard(probe_mutex_);
    for (size_t i = 0; i < device_count_; ++i) {
        const auto& location = locations_[i];
        ENSURE(location.bus != pdev->bus || location.slot != pdev->slot || location.function != pdev->function,
               Error::Busy);
    }
    if (device_count_ == devices_.size()) {
        klog::error("sdhci: too many controllers, max=%d\n", MAX_DEVICES);
        return Error::Full;
    }

    uint32_t bar = pci::read_bar(pdev->bus, pdev->slot, pdev->function, 0);
    // Only 32-bit memory BARs are supported here; do not truncate a 64-bit BAR.
    ENSURE(bar != 0 && (bar & 1U) == 0, Error::Invalid);
    ENSURE((bar & 6U) == 0, Error::NotSupported);
    uintptr_t phys = bar & ~0xFU;  // Strip attributes, including the prefetchable bit.
    ENSURE(phys != 0, Error::Invalid);
    auto mapped = vmm::map_mmio(phys, 0x1000, VM_WRITE | VM_NOCACHE);
    if (!mapped.ok()) {
        return mapped.release_error();
    }
    auto mapping = mapped.release_value();
    pci::CommandGuard command(*pdev, pci::CMD_MEMORY_SPACE);
    int index = static_cast<int>(device_count_);
    auto& device = devices_[index];
    Error error = device.init(reinterpret_cast<volatile uint8_t*>(mapping->address()), index);
    if (error == Error::None) {
        error = blk::register_device(&device);
    }
    if (error != Error::None) {
        device.shutdown();
        return error;
    }
    mappings_[index] = sys::move(mapping);
    locations_[index] = {pdev->bus, pdev->slot, pdev->function};
    command.commit();

    // Publish the stable slot only after initialization and block registration succeed.
    ++device_count_;
    return Error::None;
}

int init() {
    return Manager::init();
}

int device_count() {
    return Manager::device_count();
}

SdhciDevice* find_device() {
    return Manager::find_device(0);
}

SdhciDevice* find_device(int index) {
    return Manager::find_device(index);
}

}  // namespace sdhci
