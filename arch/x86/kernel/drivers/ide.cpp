#include "ide.h"
#include "lib/result.h"
#include "lib/stdio.h"
#include "lib/string.h"

#include <asm/arch.h>
#include <asm/drivers/i8259.h>
#include "drivers/i8259.h"
#include "drivers/intr.h"

// Global IDE devices
IdeDevice IdeManager::s_devices[ide::MAX_DEVICES] = {};
int IdeManager::s_devices_count = 0;

IdeConfig IdeManager::s_configs[ide::MAX_DEVICES] = {
    {0, 0, ide::IDE0_BASE, ide::IDE0_CTRL, IRQ_IDE1, "hda"},  // Primary Master
    {0, 1, ide::IDE0_BASE, ide::IDE0_CTRL, IRQ_IDE1, "hdb"},  // Primary Slave
    {1, 0, ide::IDE1_BASE, ide::IDE1_CTRL, IRQ_IDE2, "hdc"},  // Secondary Master
    {1, 1, ide::IDE1_BASE, ide::IDE1_CTRL, IRQ_IDE2, "hdd"},  // Secondary Slave
};

static int hd_wait_ready_on_base(uint16_t base) {
    int timeout = 100000;

    while (timeout-- > 0) {
        uint8_t status = arch_port_inb(base + ide::REG_STATUS);

        if ((status & (ide::STATUS_BSY | ide::STATUS_DRDY)) == ide::STATUS_DRDY) {
            return 0;
        }
    }

    return -1;
}

// Poll until BSY=0 and DRQ=1 (data ready for PIO transfer after a command)
static int hd_wait_drq(uint16_t base) {
    int timeout = 100000;

    while (timeout-- > 0) {
        uint8_t status = arch_port_inb(base + ide::REG_STATUS);

        if (status & ide::STATUS_ERR) {
            return -1;
        }
        if (!(status & ide::STATUS_BSY) && (status & ide::STATUS_DRQ)) {
            return 0;
        }
    }

    return -1;
}

void IdeDevice::detect(const IdeConfig* cfg) {
    this->config = cfg;

    type = blk::DeviceType::Disk;
    present = 1;

    uint16_t identify_data[256]{};
    arch_port_insw(cfg->base + ide::REG_DATA, identify_data, 256);

    info.cylinders = identify_data[1];
    info.heads = identify_data[3];
    info.sectors = identify_data[6];
    info.size = *reinterpret_cast<uint32_t*>(&identify_data[60]);
    size = info.size;
    info.valid = 1;

    strncpy(name, cfg->name, sizeof(name));
}

void IdeDevice::interrupt() {
    do {
        uint8_t status = arch_port_inb(config->base + ide::REG_STATUS);
        if (status & ide::STATUS_ERR) {
            uint8_t err = arch_port_inb(config->base + ide::REG_ERROR);
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
                arch_port_insw(config->base + ide::REG_DATA, request.buffer, ide::SECTOR_SIZE / 2);
            }
            break;
        }
        if (request.op == IdeRequest::Op::Write) {
            if (!(status & ide::STATUS_DRQ)) {
                break;
            }
            if (request.buffer) {
                arch_port_outsw(config->base + ide::REG_DATA, request.buffer, ide::SECTOR_SIZE / 2);
            }
        }
    } while (false);

    request.done = 1;
    request.waitq.wakeup_one();
}

void IdeManager::init() {
    i8259::enable(IRQ_IDE1);
    i8259::enable(IRQ_IDE2);

    cprintf("ide: probing %d channels (%d possible devices)...\n", ide::MAX_DEVICES / 2, ide::MAX_DEVICES);

    // Try to detect all 4 possible devices
    for (int i = 0; i < ide::MAX_DEVICES; i++) {
        auto& config = s_configs[i];
        uint8_t drive_sel = config.drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

        // Select drive
        arch_port_outb(config.base + ide::REG_DEVICE, drive_sel);
        arch_io_wait();

        // Send IDENTIFY command
        arch_port_outb(config.base + ide::REG_COMMAND, ide::CMD_IDENTIFY);
        arch_io_wait();

        // Check if device is present
        uint8_t status = arch_port_inb(config.base + ide::REG_STATUS);
        if (status == 0 || status == 0xFF) {
            continue;  // No device or floating bus
        }

        // Wait for BSY to clear
        if (hd_wait_ready_on_base(config.base) != 0) {
            cprintf("ide: %s: device timeout waiting for ready\n", config.name);
            continue;  // Device not ready
        }

        // Check that DRQ is set (IDENTIFY data ready) and no error
        status = arch_port_inb(config.base + ide::REG_STATUS);
        if ((status & ide::STATUS_ERR) || !(status & ide::STATUS_DRQ)) {
            cprintf("ide: %s: IDENTIFY failed (status=0x%02x)\n", config.name, status);
            continue;  // Not an ATA device (could be ATAPI or absent)
        }

        s_devices[s_devices_count].detect(&config);

        if (s_devices[s_devices_count].info.size == 0) {
            cprintf("ide: %s: device reports 0 sectors, skipping\n", config.name);
            continue;
        }

        cprintf("ide: %s: detected %d sectors (%d MB)\n", config.name, s_devices[s_devices_count].info.size,
                s_devices[s_devices_count].info.size / 2048);

        blk::register_device(&s_devices[s_devices_count]);
        s_devices_count++;
    }

    cprintf("ide: found %d device(s)\n", s_devices_count);
}

IdeDevice* IdeManager::get_device(int device_id) {
    if (device_id < 0 || device_id >= s_devices_count) {
        return nullptr;
    }
    if (!s_devices[device_id].present) {
        return nullptr;
    }
    return &s_devices[device_id];
}

int IdeManager::get_device_count() {
    return s_devices_count;
}

void IdeDevice::print_info() {
    cprintf("Device: %s (IDE)\n", name);
    cprintf("  Channel: %s, Drive: %s\n", config->channel == 0 ? "Primary" : "Secondary",
            config->drive == 0 ? "Master" : "Slave");
    cprintf("  Base I/O: 0x%x, IRQ: %d\n", config->base, config->irq);
    cprintf("  Size: %d sectors (%d MB)\n", info.size, info.size / 2048);
    cprintf("  CHS: %d/%d/%d\n", info.cylinders, info.heads, info.sectors);
    cprintf("\n");
}

Error IdeDevice::read(uint32_t block_number, void* buf, size_t block_count) {
    ENSURE_LOG(present, Error::NoDevice, "IdeDevice::read: device %s not present", name);
    ENSURE_LOG(block_number + block_count <= info.size, Error::Invalid,
               "IdeDevice::read: out of range (block %d + %d > %d)", block_number, block_count, info.size);

    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Read blocks one by one using PIO polling (no scheduler dependency)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = block_number + i;

        // Select drive first, then wait for it to become ready
        arch_port_outb(config->base + ide::REG_DEVICE, drive_sel);
        arch_io_wait();
        ENSURE_LOG(hd_wait_ready_on_base(config->base) == 0, Error::IO, "IdeDevice::read: device %s not ready", name);

        // Disable IDE interrupt for this PIO transfer (nIEN bit)
        arch_port_outb(config->ctrl, ide::CTRL_nIEN);

        arch_port_outb(config->base + ide::REG_SECTOR_COUNT, 1);
        arch_port_outb(config->base + ide::REG_LBA_LOW, lba & 0xFF);
        arch_port_outb(config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
        arch_port_outb(config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
        arch_port_outb(config->base + ide::REG_DEVICE, drive_sel | ((lba >> 24) & 0x0F));
        arch_port_outb(config->base + ide::REG_COMMAND, ide::CMD_READ);

        if (hd_wait_drq(config->base) != 0) {
            arch_port_outb(config->ctrl, 0);
            cprintf("IdeDevice::read: DRQ timeout on %s (LBA %d)\n", name, lba);
            return Error::Timeout;
        }

        arch_port_insw(config->base + ide::REG_DATA, reinterpret_cast<uint8_t*>(buf) + i * ide::SECTOR_SIZE,
                       ide::SECTOR_SIZE / 2);

        arch_port_outb(config->ctrl, 0);  // Re-enable IDE interrupt
    }

    return Error::None;
}

Error IdeDevice::write(uint32_t block_number, const void* buf, size_t block_count) {
    ENSURE_LOG(present, Error::NoDevice, "IdeDevice::write: device %s not present", name);
    ENSURE_LOG(block_number + block_count <= info.size, Error::Invalid,
               "IdeDevice::write: out of range (block %d + %d > %d)", block_number, block_count, info.size);

    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Write blocks one by one using PIO polling (no scheduler dependency)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = block_number + i;

        // Select drive first, then wait for it to become ready
        arch_port_outb(config->base + ide::REG_DEVICE, drive_sel);
        arch_io_wait();
        ENSURE_LOG(hd_wait_ready_on_base(config->base) == 0, Error::IO, "IdeDevice::write: device %s not ready", name);

        // Disable IDE interrupt for this PIO transfer (nIEN bit)
        arch_port_outb(config->ctrl, ide::CTRL_nIEN);

        arch_port_outb(config->base + ide::REG_SECTOR_COUNT, 1);
        arch_port_outb(config->base + ide::REG_LBA_LOW, lba & 0xFF);
        arch_port_outb(config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
        arch_port_outb(config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
        arch_port_outb(config->base + ide::REG_DEVICE, drive_sel | ((lba >> 24) & 0x0F));
        arch_port_outb(config->base + ide::REG_COMMAND, ide::CMD_WRITE);

        // Wait for drive to signal it is ready to accept data
        if (hd_wait_drq(config->base) != 0) {
            arch_port_outb(config->ctrl, 0);
            cprintf("IdeDevice::write: DRQ timeout on %s (LBA %d)\n", name, lba);
            return Error::Timeout;
        }

        arch_port_outsw(config->base + ide::REG_DATA,
                        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf)) + i * ide::SECTOR_SIZE,
                        ide::SECTOR_SIZE / 2);

        // Wait for write to complete
        if (hd_wait_ready_on_base(config->base) != 0) {
            arch_port_outb(config->ctrl, 0);
            cprintf("IdeDevice::write: completion timeout on %s (LBA %d)\n", name, lba);
            return Error::Timeout;
        }

        arch_port_outb(config->ctrl, 0);  // Re-enable IDE interrupt
    }

    return Error::None;
}

void IdeManager::interrupt_handler(int channel) {
    for (int i = 0; i < s_devices_count; i++) {
        IdeDevice& dev = s_devices[i];

        if (!dev.present || dev.config->channel != channel) {
            continue;
        }
        if (dev.request.op == IdeRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}
