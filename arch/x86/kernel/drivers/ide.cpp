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

void IdeDevice::detect(const IdeConfig* cfg) {
    this->config = cfg;

    type = blk::DeviceType::Disk;
    present = 1;

    uint16_t identify_data[256]{};
    arch_port_read16_buffer(cfg->base + ide::REG_DATA, identify_data, 256);

    info.cylinders = identify_data[1];
    info.heads = identify_data[3];
    info.sectors = identify_data[6];
    info.block_count = static_cast<uint32_t>(identify_data[60]) | (static_cast<uint32_t>(identify_data[61]) << 16);
    block_count = info.block_count;
    info.valid = 1;

    strncpy(name, cfg->name, sizeof(name));
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
        arch_port_write8(config.ctrl, 0);
        PioInterruptMask interrupt_mask(config.ctrl);
        uint8_t drive_sel = config.drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

        // Select drive
        arch_port_write8(config.base + ide::REG_DEVICE, drive_sel);
        arch_io_wait();

        // Send IDENTIFY command
        arch_port_write8(config.base + ide::REG_COMMAND, ide::CMD_IDENTIFY);
        arch_io_wait();

        // Check if device is present
        uint8_t status = arch_port_read8(config.base + ide::REG_STATUS);
        if (status == 0 || status == 0xFF) {
            continue;  // No device or floating bus
        }

        Error ready = wait_status(config.base, ide::STATUS_DRQ);
        if (ready != Error::None) {
            cprintf("ide: %s: IDENTIFY failed: %s\n", config.name, error_str(ready));
            if (first_error == Error::None) {
                first_error = ready;
            }
            continue;
        }

        devices_[device_count_].detect(&config);

        if (devices_[device_count_].info.block_count == 0 || devices_[device_count_].info.block_count > (1U << 28)) {
            devices_[device_count_].present = 0;
            devices_[device_count_].info.valid = 0;
            cprintf("ide: %s: unsupported sector count %u\n", config.name, devices_[device_count_].info.block_count);
            if (first_error == Error::None) {
                first_error = devices_[device_count_].info.block_count == 0 ? Error::NoDevice : Error::NotSupported;
            }
            continue;
        }

        cprintf("ide: %s: detected %d sectors (%d MB)\n", config.name, devices_[device_count_].info.block_count,
                devices_[device_count_].info.block_count / 2048);

        Error registered = blk::register_device(&devices_[device_count_]);
        if (registered != Error::None) {
            cprintf("ide: failed to register %s: %s (%d)\n", config.name, error_str(registered),
                    static_cast<int>(registered));
            devices_[device_count_].present = 0;
            devices_[device_count_].info.valid = 0;
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
    if (!devices_[index].present) {
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
    ENSURE_LOG(present, Error::NoDevice, "IdeDevice::read: device %s not present", name);
    ENSURE_LOG(start_lba <= info.block_count && block_count <= info.block_count - start_lba, Error::Invalid,
               "IdeDevice::read: out of range (block %d + %d > %d)", start_lba, block_count, info.block_count);

    ENSURE(buf || block_count == 0, Error::Invalid);
    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Read blocks one by one using PIO polling (no scheduler dependency)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = start_lba + i;

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
        arch_port_write8(config->base + ide::REG_COMMAND, ide::CMD_READ);

        TRY(wait_status(config->base, ide::STATUS_DRQ));

        arch_port_read16_buffer(config->base + ide::REG_DATA, reinterpret_cast<uint8_t*>(buf) + i * ide::SECTOR_SIZE,
                                ide::SECTOR_SIZE / 2);
    }

    return Error::None;
}

Error IdeDevice::write(uint32_t start_lba, const void* buf, size_t block_count) {
    ENSURE_LOG(present, Error::NoDevice, "IdeDevice::write: device %s not present", name);
    ENSURE_LOG(start_lba <= info.block_count && block_count <= info.block_count - start_lba, Error::Invalid,
               "IdeDevice::write: out of range (block %d + %d > %d)", start_lba, block_count, info.block_count);

    ENSURE(buf || block_count == 0, Error::Invalid);
    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Write blocks one by one using PIO polling (no scheduler dependency)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = start_lba + i;

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
        arch_port_write8(config->base + ide::REG_COMMAND, ide::CMD_WRITE);

        // Wait for drive to signal it is ready to accept data
        TRY(wait_status(config->base, ide::STATUS_DRQ));

        arch_port_write16_buffer(config->base + ide::REG_DATA,
                                 const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf)) + i * ide::SECTOR_SIZE,
                                 ide::SECTOR_SIZE / 2);

        // Wait for write to complete
        TRY(wait_status(config->base, ide::STATUS_DRDY));
    }

    return Error::None;
}

void IdeManager::interrupt_handler(int channel) {
    for (int i = 0; i < device_count_; i++) {
        IdeDevice& dev = devices_[i];

        if (!dev.present || dev.config->channel != channel) {
            continue;
        }
        if (dev.request.op == IdeRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}
