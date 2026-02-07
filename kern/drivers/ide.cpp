#include "ide.h"
#include "stdio.h"
#include "string.h"

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>
#include <arch/x86/drivers/i8259.h>
#include "pic.h"
#include "../sched/sched.h"
#include "../drivers/intr.h"

// Global IDE devices
IdeDevice IdeManager::s_ide_devices[ide::MAX_DEVICES] = {};
int IdeManager::s_ide_devices_count = 0;

IdeConfig IdeManager::s_ide_configs[ide::MAX_DEVICES] = {
    {0, 0, ide::IDE0_BASE, ide::IDE0_CTRL, IRQ_IDE1, "hda"},  // Primary Master
    {0, 1, ide::IDE0_BASE, ide::IDE0_CTRL, IRQ_IDE1, "hdb"},  // Primary Slave
    {1, 0, ide::IDE1_BASE, ide::IDE1_CTRL, IRQ_IDE2, "hdc"},  // Secondary Master
    {1, 1, ide::IDE1_BASE, ide::IDE1_CTRL, IRQ_IDE2, "hdd"},  // Secondary Slave
};

static int hd_wait_ready_on_base(uint16_t base) {
    int timeout = 100000;
    
    while (timeout-- > 0) {
        uint8_t status = inb(base + ide::REG_STATUS);

        if ((status & (ide::STATUS_BSY | ide::STATUS_DRDY)) == ide::STATUS_DRDY) {
            return 0;
        }
    }
    
    return -1;
}

static int hd_wait_data_on_base(uint16_t base) {
    int timeout = 100000;
    
    while (timeout-- > 0) {
        uint8_t status = inb(base + ide::REG_STATUS);

        if ((status & (ide::STATUS_BSY | ide::STATUS_DRQ)) == ide::STATUS_DRQ) {
            return 0;
        }

        if (status & ide::STATUS_ERR) {
            return -1;
        }
    }
    
    return -1;
}

void IdeDevice::detect(const IdeConfig* config) {
    m_config = config;
    
    m_type = blk::DeviceType::Disk;
    m_present = 1;

    uint16_t identifyData[256]{};
    insw(m_config->base + ide::REG_DATA, identifyData, 256);

    m_info.cylinders = identifyData[1];
    m_info.heads = identifyData[3];
    m_info.sectors = identifyData[6];
    m_info.size = *reinterpret_cast<uint32_t*>(&identifyData[60]);
    m_info.valid = 1;

    strncpy(m_name, m_config->name, sizeof(m_name));
}

void IdeDevice::interupt() {
    do {
        uint8_t status = inb(m_config->base + ide::REG_STATUS);
        if (status & ide::STATUS_ERR) {
            uint8_t err = inb(m_config->base + ide::REG_ERROR);
            cprintf("hd_intr: disk error on %s (status=0x%02x, error=0x%02x)\n", m_name, status, err);
            m_request.err = -1;
            break;
        }

        // Read operation: read data when DRQ is set
        if (m_request.op == IdeRequest::Op::Read) {
            if (!(status & ide::STATUS_DRQ)) {
                break;
            }
            if (m_request.buffer) {
                insw(m_config->base + ide::REG_DATA, m_request.buffer, ide::SECTOR_SIZE / 2);
            }
            break;
        }
        else if (m_request.op == IdeRequest::Op::Write) {
            if (!(status & ide::STATUS_DRQ)) {
                break;
            }
            if (m_request.buffer) {
                outsw(m_config->base + ide::REG_DATA, m_request.buffer, ide::SECTOR_SIZE / 2);
            }
        }
    } while (0);

    m_request.done = 1;
    if (m_request.waiting) {
        m_request.waiting->wakeup();
    }
}

void IdeManager::init(void) {

    pic::enable(IRQ_IDE1);
    pic::enable(IRQ_IDE2);

    // Try to detect all 4 possible devices
    for (int i = 0; i < ide::MAX_DEVICES; i++) {
        auto& config = s_ide_configs[i];
        uint8_t driveSel = config.drive ? ide::DEV_SLAVE : ide::DEV_MASTER;
        outb(config.base + ide::REG_DEVICE, 0xA0 | (driveSel << 4));
        io_wait();

        outb(config.base + ide::REG_COMMAND, ide::CMD_IDENTIFY);
        io_wait();

        // Check if device is present
        uint8_t status = inb(config.base + ide::REG_STATUS);
        if (status == 0) {
            continue; // No device
        }

        // Wait for BSY to clear
        if (hd_wait_ready_on_base(config.base) != 0) {
            continue; // Device not ready
        }
        s_ide_devices[s_ide_devices_count++].detect(&config);
    }
    
    cprintf("hd_init: found %d device(s)\n", s_ide_devices_count);
}

IdeDevice* IdeManager::get_device(int deviceID) {
    if (deviceID < 0 || deviceID >= s_ide_devices_count) {
        return nullptr;
    }
    if (!s_ide_devices[deviceID].m_present) {
        return nullptr;
    }
    return &s_ide_devices[deviceID];
}

int IdeManager::get_device_count() {
    return s_ide_devices_count;
}

int IdeDevice::read(uint32_t blockNumber, void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("IdeDevice::read: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("IdeDevice::read: out of range (block %d + %d > %d)\n", blockNumber, blockCount, m_info.size);
        return -1;
    }
    
    uint8_t driveSel = m_config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;
    
    // Read blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(m_config->base) != 0) {
            cprintf("IdeDevice::read: device %s not ready\n", m_name);
            return -1;
        }

        {
            InterruptsGuard guard;

            m_request.reset();
            m_request.buffer = reinterpret_cast<uint8_t*>(buf) + i * ide::SECTOR_SIZE;
            m_request.op = IdeRequest::Op::Read;
            m_request.waiting = TaskManager::get_current();

            // Set sector count and LBA address
            outb(m_config->base + ide::REG_SECTOR_COUNT, 1);
            outb(m_config->base + ide::REG_LBA_LOW, lba & 0xFF);
            outb(m_config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
            outb(m_config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
            outb(m_config->base + ide::REG_DEVICE, driveSel | ((lba >> 24) & 0x0F));

            // Send read command (will trigger interrupt)
            outb(m_config->base + ide::REG_COMMAND, ide::CMD_READ);
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!m_request.done) {
            {
                InterruptsGuard guard;
                if (m_request.done) break;  // Re-check with interrupts disabled
                TaskManager::get_current()->m_state = ProcessState::Sleeping;
            }
            TaskManager::schedule();
        }

        if (m_request.err) {
            m_request.reset();
            cprintf("IdeDevice::read: error reading block %d from %s\n", lba, m_name);
            return -1;
        }

        m_request.reset();
    }
    
    return 0;
}

int IdeDevice::write(uint32_t blockNumber, const void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("IdeDevice::write: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("IdeDevice::write: out of range (block %d + %d > %d)\n", blockNumber, blockCount, m_info.size);
        return -1;
    }
    
    uint8_t driveSel = m_config->drive ? ide::DEV_SLAVE : ide::DEV_MASTER;
    
    // Write blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(m_config->base) != 0) {
            cprintf("IdeDevice::write: device %s not ready\n", m_name);
            return -1;
        }

        {
            InterruptsGuard guard;

            m_request.reset();
            m_request.buffer = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf) + i * ide::SECTOR_SIZE);
            m_request.op = IdeRequest::Op::Write;
            m_request.waiting = TaskManager::get_current();

            outb(m_config->base + ide::REG_SECTOR_COUNT, 1);
            outb(m_config->base + ide::REG_LBA_LOW, lba & 0xFF);
            outb(m_config->base + ide::REG_LBA_MID, (lba >> 8) & 0xFF);
            outb(m_config->base + ide::REG_LBA_HIGH, (lba >> 16) & 0xFF);
            outb(m_config->base + ide::REG_DEVICE, driveSel | ((lba >> 24) & 0x0F));

            // Send write command
            outb(m_config->base + ide::REG_COMMAND, ide::CMD_WRITE);

            // Wait for device ready to receive data (within critical section)
            if (hd_wait_ready_on_base(m_config->base) != 0) {
                m_request.reset();
                return -1;  // guard destructor will restore interrupts
            }

            // Write the data
            outsw(m_config->base + ide::REG_DATA, m_request.buffer, ide::SECTOR_SIZE / 2);
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!m_request.done) {
            {
                InterruptsGuard guard;
                if (m_request.done) break;  // Re-check with interrupts disabled
                TaskManager::get_current()->m_state = ProcessState::Sleeping;
            }
            TaskManager::schedule();
        }

        if (m_request.err) {
            m_request.reset();
            cprintf("IdeDevice::write: error writing block %d to %s\n", lba, m_name);
            return -1;
        }

        m_request.reset();
    }
    
    return 0;
}

/**
 * Handle IDE interrupt for specified channel
 * @param channel IDE channel (0 = primary, 1 = secondary)
 */
void IdeManager::interrupt_handler(int channel) {
    for (int i = 0; i < s_ide_devices_count; i++) {
        IdeDevice& dev = s_ide_devices[i];

        if (!dev.m_present || dev.m_config->channel != channel) {
            continue;
        }
        if (dev.m_request.op == IdeRequest::Op::None) {
            continue;
        }

        dev.interupt();
    }
}
