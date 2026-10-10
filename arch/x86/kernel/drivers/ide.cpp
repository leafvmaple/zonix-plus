#include "ide.h"
#include "lib/result.h"
#include "lib/stdio.h"
#include "lib/string.h"

#include <asm/arch.h>
#include <asm/drivers/i8259.h>
#include "drivers/i8259.h"
#include "drivers/intr.h"

// Permanent, address-stable devices owned by the IDE manager.
IdeDevice IdeManager::devices_[ide::MAX_DEVICES] = {};
int IdeManager::device_count_ = 0;

IdeConfig IdeManager::configs_[ide::MAX_DEVICES] = {
    {0, 0, ide::IDE0_BASE, ide::IDE0_CTRL, IRQ_IDE1, "hda"},  // Primary Master
    {0, 1, ide::IDE0_BASE, ide::IDE0_CTRL, IRQ_IDE1, "hdb"},  // Primary Slave
    {1, 0, ide::IDE1_BASE, ide::IDE1_CTRL, IRQ_IDE2, "hdc"},  // Secondary Master
    {1, 1, ide::IDE1_BASE, ide::IDE1_CTRL, IRQ_IDE2, "hdd"},  // Secondary Slave
};

namespace {

constexpr int COMMAND_POLL_LIMIT = 100000;

Error wait_status(uint16_t base, uint8_t required) {
    for (int polls = COMMAND_POLL_LIMIT; polls > 0; --polls) {
        uint8_t status = arch_port_read8(base + ide::REG_STATUS);
        if (status == 0 || status == 0xFF) {
            return Error::NoDevice;
        }
        if ((status & ide::STATUS_BSY) == 0) {
            ENSURE((status & (ide::STATUS_ERR | ide::STATUS_DF)) == 0, Error::Io);
            if ((status & required) == required) {
                return Error::None;
            }
        }
        arch_spin_hint();
    }
    return Error::Timeout;
}

bool identify_is_atapi(uint16_t base) {
    uint8_t status = arch_port_read8(base + ide::REG_STATUS);
    return (status & (ide::STATUS_BSY | ide::STATUS_DF | ide::STATUS_ERR)) == ide::STATUS_ERR &&
           arch_port_read8(base + ide::REG_ERROR) == ide::ERROR_ABORTED &&
           arch_port_read8(base + ide::REG_LBA_MID) == ide::ATAPI_SIGNATURE_MID &&
           arch_port_read8(base + ide::REG_LBA_HIGH) == ide::ATAPI_SIGNATURE_HIGH;
}

// The IDE driver owns Device Control, initializes it to zero, and uses only
// nIEN during synchronous PIO. The register is write-only (reads are status).
class PioInterruptMask {
public:
    explicit PioInterruptMask(uint16_t control) : control_(control) {
        arch_port_write8(control_, ide::CTRL_INTERRUPT_DISABLE);
    }
    ~PioInterruptMask() { arch_port_write8(control_, 0); }
    PioInterruptMask(const PioInterruptMask&) = delete;
    PioInterruptMask& operator=(const PioInterruptMask&) = delete;

private:
    uint16_t control_;
};

}  // namespace

Result<bool> IdeDevice::detect_locked(const IdeConfig* cfg) {
    ENSURE(cfg && cfg->channel < ide::CHANNEL_COUNT, Error::Invalid);
    ENSURE(state_ == blk::DeviceState::Offline, Error::Busy);
    this->config = cfg;
    state_ = blk::DeviceState::Initializing;
    info = {};
    block_count = 0;
    type = blk::DeviceType::Disk;
    strncpy(name, cfg->name, sizeof(name));

    auto detected = identify_locked();
    state_ = detected.ok() && detected.value() ? blk::DeviceState::Ready : blk::DeviceState::Offline;
    return detected;
}

Result<bool> IdeDevice::identify_locked() {
    const auto& cfg = *config;
    arch_port_write8(cfg.ctrl, 0);
    PioInterruptMask interrupt_mask(cfg.ctrl);
    uint8_t drive_sel = cfg.drive ? ide::DEV_SLAVE : ide::DEV_MASTER;
    arch_port_write8(cfg.base + ide::REG_DEVICE, drive_sel);
    arch_io_wait();

    // Packet devices replace these cleared bytes with their signature when
    // aborting ATA IDENTIFY. A stale signature must not hide an ATA error.
    arch_port_write8(cfg.base + ide::REG_LBA_LOW, 0);
    arch_port_write8(cfg.base + ide::REG_LBA_MID, 0);
    arch_port_write8(cfg.base + ide::REG_LBA_HIGH, 0);
    arch_port_write8(cfg.base + ide::REG_COMMAND, ide::CMD_IDENTIFY);
    arch_io_wait();

    uint8_t status = arch_port_read8(cfg.base + ide::REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return false;
    }
    Error ready = wait_status(cfg.base, ide::STATUS_DRQ);
    if (ready == Error::Io && identify_is_atapi(cfg.base)) {
        cprintf("ide: %s: skipping unsupported ATAPI device\n", cfg.name);
        return false;
    }
    TRY(ready);

    uint16_t identify_data[256]{};
    arch_port_read16_buffer(cfg.base + ide::REG_DATA, identify_data, 256);

    info.cylinders = identify_data[1];
    info.heads = identify_data[3];
    info.sectors = identify_data[6];
    info.block_count = static_cast<uint32_t>(identify_data[60]) | (static_cast<uint32_t>(identify_data[61]) << 16);
    block_count = info.block_count;
    ENSURE(block_count != 0, Error::NoDevice);
    ENSURE(block_count <= (1U << 28), Error::NotSupported);
    return true;
}

void IdeDevice::interrupt() {
    do {
        uint8_t status = arch_port_read8(config->base + ide::REG_STATUS);
        if (status & ide::STATUS_ERR) {
            uint8_t err = arch_port_read8(config->base + ide::REG_ERROR);
            cprintf("hd_intr: disk error on %s (status=0x%02x, error=0x%02x)\n", name, status, err);
            request.err = -1;
            break;
        }

        // Read operation: read data when DRQ is set
        if (request.op == IdeRequest::Op::Read) {
            if (!(status & ide::STATUS_DRQ)) {
                break;
            }
            if (request.buffer) {
                arch_port_read16_buffer(config->base + ide::REG_DATA, request.buffer, ide::SECTOR_SIZE / 2);
            }
            break;
        }
        if (request.op == IdeRequest::Op::Write) {
            if (!(status & ide::STATUS_DRQ)) {
                break;
            }
            if (request.buffer) {
                arch_port_write16_buffer(config->base + ide::REG_DATA, request.buffer, ide::SECTOR_SIZE / 2);
            }
        }
    } while (false);

    request.done = 1;
    request.waitq.wakeup_one();
}

Error IdeManager::init() {
    sys::lock_guard<Mutex> probe_guard(probe_mutex_);
    // Published objects are permanent. Retry skips their configurations rather
    // than registering duplicates or overwriting a live device slot.
    Error first_error = Error::None;
    i8259::enable(IRQ_IDE1);
    i8259::enable(IRQ_IDE2);

    cprintf("ide: probing %d channels (%d possible devices)...\n", ide::MAX_DEVICES / 2, ide::MAX_DEVICES);

    // Try to detect all 4 possible devices
    for (int i = 0; i < ide::MAX_DEVICES; i++) {
        auto& config = configs_[i];
        bool published = false;
        for (int j = 0; j < device_count_; ++j) {
            if (devices_[j].config == &config) {
                published = true;
                break;
            }
        }
        if (published) {
            continue;
        }
        ENSURE(device_count_ < ide::MAX_DEVICES, Error::Full);
        sys::lock_guard<Mutex> guard(channel_mutexes_[config.channel]);
        auto& device = devices_[device_count_];
        auto detected = device.detect_locked(&config);
        if (!detected.ok()) {
            Error error = detected.release_error();
            cprintf("ide: %s: IDENTIFY failed: %s\n", config.name, error_str(error));
            if (first_error == Error::None) {
                first_error = error;
            }
            continue;
        }
        if (!detected.value()) {
            continue;
        }

        cprintf("ide: %s: detected %d sectors (%d MB)\n", config.name, devices_[device_count_].info.block_count,
                devices_[device_count_].info.block_count / 2048);

        Error registered = blk::register_device(&devices_[device_count_]);
        if (registered != Error::None) {
            cprintf("ide: failed to register %s: %s (%d)\n", config.name, error_str(registered),
                    static_cast<int>(registered));
            device.state_ = blk::DeviceState::Offline;
            return registered;
        }
        device_count_++;
    }

    cprintf("ide: found %d device(s)\n", device_count_);
    return first_error;
}

IdeDevice* IdeManager::find_device(int index) {
    if (index < 0 || index >= device_count_) {
        return nullptr;
    }
    if (devices_[index].state() != blk::DeviceState::Ready) {
        return nullptr;
    }
    return &devices_[index];
}

int IdeManager::device_count() {
    return device_count_;
}

void IdeDevice::print_info() {
    cprintf("Device: %s (IDE)\n", name);
    cprintf("  Channel: %s, Drive: %s\n", config->channel == 0 ? "Primary" : "Secondary",
            config->drive == 0 ? "Master" : "Slave");
    cprintf("  Base I/O: 0x%x, IRQ: %d\n", config->base, config->irq);
    cprintf("  Size: %d sectors (%d MB)\n", info.block_count, info.block_count / 2048);
    cprintf("  CHS: %d/%d/%d\n", info.cylinders, info.heads, info.sectors);
    cprintf("\n");
}

Error IdeDevice::read(uint32_t start_lba, void* buf, size_t block_count) {
    return transfer_blocks(start_lba, buf, block_count, false);
}

Error IdeDevice::write(uint32_t start_lba, const void* buf, size_t block_count) {
    return transfer_blocks(start_lba, const_cast<void*>(buf), block_count, true);
}

Error IdeDevice::transfer_blocks(uint32_t start_lba, void* buf, size_t block_count, bool write) {
    ENSURE(config && config->channel < ide::CHANNEL_COUNT, Error::NoDevice);
    sys::lock_guard<Mutex> guard(IdeManager::channel_mutexes_[config->channel]);
    ENSURE(state_ == blk::DeviceState::Ready, Error::NoDevice);
    ENSURE_LOG(start_lba <= info.block_count && block_count <= info.block_count - start_lba, Error::Invalid,
               "IDE request out of range (block %d + %d > %d)", start_lba, block_count, info.block_count);

    ENSURE(buf || block_count == 0, Error::Invalid);
    // Retain the channel for all sectors in this PIO request.
    for (size_t i = 0; i < block_count; i++) {
        Error error = transfer_sector_locked(start_lba + i, static_cast<uint8_t*>(buf) + i * ide::SECTOR_SIZE, write);
        if (error != Error::None) {
            state_ = blk::DeviceState::Offline;
            return error;
        }
    }

    return Error::None;
}

Error IdeDevice::transfer_sector_locked(uint32_t lba, uint8_t* buf, bool write) {
    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Select drive first, then wait for it to become ready
    arch_port_write8(config->base + ide::REG_DEVICE, drive_sel);
    arch_io_wait();
    TRY(wait_status(config->base, ide::STATUS_DRDY));

    // Disable IDE interrupt for this PIO transfer (nIEN bit)
    PioInterruptMask interrupt_mask(config->ctrl);

    arch_port_write8(config->base + ide::REG_SECTOR_COUNT, 1);
    arch_port_write8(config->base + ide::REG_LBA_LOW, lba & 0xFF);
    arch_port_write8(config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
    arch_port_write8(config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
    arch_port_write8(config->base + ide::REG_DEVICE, drive_sel | ((lba >> 24) & 0x0F));
    arch_port_write8(config->base + ide::REG_COMMAND, write ? ide::CMD_WRITE : ide::CMD_READ);

    // Wait for drive to signal it is ready to accept data
    TRY(wait_status(config->base, ide::STATUS_DRQ));

    if (write) {
        arch_port_write16_buffer(config->base + ide::REG_DATA, buf, ide::SECTOR_SIZE / 2);
        TRY(wait_status(config->base, ide::STATUS_DRDY));
    } else {
        arch_port_read16_buffer(config->base + ide::REG_DATA, buf, ide::SECTOR_SIZE / 2);
    }

    return Error::None;
}

void IdeManager::interrupt_handler(int channel) {
    for (int i = 0; i < device_count_; i++) {
        IdeDevice& dev = devices_[i];

        if (dev.state() != blk::DeviceState::Ready || dev.config->channel != channel) {
            continue;
        }
        if (dev.request.op == IdeRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}
