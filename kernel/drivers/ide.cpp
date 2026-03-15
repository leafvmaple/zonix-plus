#include "ide.h"
#include "lib/stdio.h"
#include "lib/string.h"

#include <asm/arch.h>
#include <asm/drivers/i8259.h>
#include "pic.h"
#include "intr.h"

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

void IdeManager::init(void) {
    pic::enable(IRQ_IDE1);
    pic::enable(IRQ_IDE2);

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
            continue;  // Device not ready
        }

        // Check that DRQ is set (IDENTIFY data ready) and no error
        status = arch_port_inb(config.base + ide::REG_STATUS);
        if ((status & ide::STATUS_ERR) || !(status & ide::STATUS_DRQ)) {
            continue;  // Not an ATA device (could be ATAPI or absent)
        }

        s_devices[s_devices_count].detect(&config);
        // Only count the device if it reported a valid size
        if (s_devices[s_devices_count].info.size > 0) {
            s_devices_count++;
        }
    }

    cprintf("hd_init: found %d device(s)\n", s_devices_count);
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

void IdeDevice::test() {
    if (!present) {
        cprintf("IdeDevice::test: %s not present\n", name);
        return;
    }

    uint8_t buf[ide::SECTOR_SIZE] = {};
    int rc = read(0, buf, 1);
    cprintf("IdeDevice::test: %s %s\n", name, rc == 0 ? "ok" : "failed");
}

void IdeDevice::test_interrupt() {
    if (!present) {
        cprintf("IdeDevice::test_interrupt: %s not present\n", name);
        return;
    }

    IdeDevice* first = IdeManager::get_device(0);
    if (first != this) {
        return;
    }
    IdeManager::test_interrupt();
}

int IdeDevice::read(uint32_t block_number, void* buf, size_t block_count) {
    if (!present) {
        cprintf("IdeDevice::read: device %s not present\n", name);
        return -1;
    }

    if (block_number + block_count > info.size) {
        cprintf("IdeDevice::read: out of range (block %d + %d > %d)\n", block_number, block_count, info.size);
        return -1;
    }

    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Read blocks one by one (interrupt-driven)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = block_number + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(config->base) != 0) {
            cprintf("IdeDevice::read: device %s not ready\n", name);
            return -1;
        }

        {
            intr::Guard guard;

            request.reset();
            request.buffer = reinterpret_cast<uint8_t*>(buf) + i * ide::SECTOR_SIZE;
            request.op = IdeRequest::Op::Read;

            // Set sector count and LBA address
            arch_port_outb(config->base + ide::REG_SECTOR_COUNT, 1);
            arch_port_outb(config->base + ide::REG_LBA_LOW, lba & 0xFF);
            arch_port_outb(config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
            arch_port_outb(config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
            arch_port_outb(config->base + ide::REG_DEVICE, drive_sel | ((lba >> 24) & 0x0F));

            // Send read command (will trigger interrupt)
            arch_port_outb(config->base + ide::REG_COMMAND, ide::CMD_READ);
        }

        // Wait for interrupt completion
        while (!request.done) {
            request.waitq.sleep();
        }

        if (request.err) {
            request.reset();
            cprintf("IdeDevice::read: error reading block %d from %s\n", lba, name);
            return -1;
        }

        request.reset();
    }

    return 0;
}

int IdeDevice::write(uint32_t block_number, const void* buf, size_t block_count) {
    if (!present) {
        cprintf("IdeDevice::write: device %s not present\n", name);
        return -1;
    }

    if (block_number + block_count > info.size) {
        cprintf("IdeDevice::write: out of range (block %d + %d > %d)\n", block_number, block_count, info.size);
        return -1;
    }

    uint8_t drive_sel = config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;

    // Write blocks one by one (interrupt-driven)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = block_number + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(config->base) != 0) {
            cprintf("IdeDevice::write: device %s not ready\n", name);
            return -1;
        }

        {
            intr::Guard guard;

            request.reset();
            request.buffer = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf) + i * ide::SECTOR_SIZE);
            request.op = IdeRequest::Op::Write;

            arch_port_outb(config->base + ide::REG_SECTOR_COUNT, 1);
            arch_port_outb(config->base + ide::REG_LBA_LOW, lba & 0xFF);
            arch_port_outb(config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
            arch_port_outb(config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
            arch_port_outb(config->base + ide::REG_DEVICE, drive_sel | ((lba >> 24) & 0x0F));

            // Send write command
            arch_port_outb(config->base + ide::REG_COMMAND, ide::CMD_WRITE);

            // Wait for device ready to receive data (within critical section)
            if (hd_wait_ready_on_base(config->base) != 0) {
                request.reset();
                return -1;  // guard destructor will restore interrupts
            }

            // Write the data
            arch_port_outsw(config->base + ide::REG_DATA, request.buffer, ide::SECTOR_SIZE / 2);
        }

        // Wait for interrupt completion
        while (!request.done) {
            request.waitq.sleep();
        }

        if (request.err) {
            request.reset();
            cprintf("IdeDevice::write: error writing block %d to %s\n", lba, name);
            return -1;
        }

        request.reset();
    }

    return 0;
}

/**
 * Handle IDE interrupt for specified channel
 * @param channel IDE channel (0 = primary, 1 = secondary)
 */
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
