#include "ahci.h"
#include "pci.h"
#include "stdio.h"
#include "string.h"

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>
#include <arch/x86/asm/pg.h>
#include <arch/x86/drivers/i8259.h>
#include "pic.h"
#include "../sched/sched.h"
#include "../drivers/intr.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"

AhciDevice AhciManager::s_ahci_devices[ahci::MAX_DEVICES] = {};
int AhciManager::s_ahci_devices_count = 0;

uint32_t AhciManager::s_ahci_base = ahci::AHCI_BAR_BASE;

AhciPortConfig AhciManager::s_ahci_port_configs[ahci::MAX_DEVICES] = {
    {0, IRQ_IDE1, "sda"},  // AHCI port 0
    {1, IRQ_IDE1, "sdb"},  // AHCI port 1
    {2, IRQ_IDE1, "sdc"},  // AHCI port 2
    {3, IRQ_IDE1, "sdd"},  // AHCI port 3
};

static uint32_t pci_find_ahci_bar() {
    PCILocation loc{};

    if (!PCILocation::find_device_by_class(pci::CLASS_MASS_STORAGE, pci::SUBCLASS_SATA, pci::INTERFACE_AHCI, &loc)) {
        cprintf("AHCI: No AHCI controller found on PCI bus\n");
        return 0;
    }

    uint32_t bar5 = loc.read_bar(5);
    loc.enable_bus_master();
    uint32_t abar = bar5 & 0xFFFFFFF0;
    
    cprintf("AHCI: Found controller at PCI %02x:%02x.%x, ABAR=0x%08x\n", loc.bus, loc.device, loc.function, abar);
    
    return abar;
}

static inline uint32_t mmio_read32(uint32_t base, uint32_t offset) {
    return *(volatile uint32_t*)(base + offset);
}

static inline void mmio_write32(uint32_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(base + offset) = value;
}

static int ahci_wait_port_ready(uint32_t portBase, int timeout_ms) {
    int timeout = timeout_ms * 1000;  // Convert to iterations
    
    while (timeout-- > 0) {
        uint32_t ssts = mmio_read32(portBase, ahci::PORT_SATA_STS);

        if ((ssts & ahci::SATA_STS_DET_MASK) == ahci::SATA_STS_DET_PRESENT) {
            return 0;
        }
    }
    
    return -1;
}

static int ahci_enable_port(uint32_t portBase) {

    uint32_t cmd = mmio_read32(portBase, ahci::PORT_CMD_STAT);
    cmd |= ahci::CMD_FRE;  // Enable FIS receive
    mmio_write32(portBase, ahci::PORT_CMD_STAT, cmd);

    for (int i = 0; i < 100000; i++);

    cmd |= ahci::CMD_ST;
    mmio_write32(portBase, ahci::PORT_CMD_STAT, cmd);
    
    return 0;
}

void AhciDevice::detect(const AhciPortConfig* config, uint32_t mmioBase) {
    m_config = config;
    m_port_base = mmioBase + 0x100 + (config->port_num * 0x80);  // AHCI port offset
    
    m_type = blk::DeviceType::Disk;
    m_present = 1;

    m_info.size = 131072;  // TODO
    m_info.serial = config->port_num;
    m_info.model = 0;
    m_info.valid = 1;

    strncpy(m_name, m_config->name, sizeof(m_name));
}

void AhciDevice::interrupt() {
    uint32_t is = mmio_read32(m_port_base, ahci::PORT_IS);
 
    mmio_write32(m_port_base, ahci::PORT_IS, is);
    
    if (is & ahci::IS_DHRS) {
        if (m_request.op == AhciRequest::Op::Read) {
            // Data is ready in buffer
        } else if (m_request.op == AhciRequest::Op::Write) {
            // Write completed
        }
    }
    
    if (is & ahci::IS_PCS) {
        cprintf("ahci%d: port connect change detected\n", m_config->port_num);
    }
    
    if (is & ahci::IS_OFS) {
        m_request.err = -1;
    }
    
    m_request.done = 1;
    if (m_request.waiting) {
        m_request.waiting->wakeup();
    }
}

void AhciManager::init(void) {
    extern pde_t* boot_pgdir;
    
    cprintf("AHCI: Initializing AHCI controller...\n");

    uint32_t s_ahci_base = pci_find_ahci_bar();
    
    if (s_ahci_base == 0) {
        cprintf("AHCI: No AHCI controller found on PCI bus\n");
        return;
    }
    
    // Map AHCI MMIO region (typically 8KB, use 64KB to be safe)
    // Identity map: virtual address = physical address for simplicity
    pgdir_init(boot_pgdir, s_ahci_base, 0x10000, s_ahci_base, PTE_W | PTE_PCD | PTE_PWT);
    
    // Check if AHCI controller is present by reading version register
    uint32_t version = mmio_read32(s_ahci_base, ahci::AHCI_VS);
    
    // If version is 0x00000000 or 0xFFFFFFFF, controller is not present
    if (version == 0x00000000 || version == 0xFFFFFFFF) {
        cprintf("AHCI: No AHCI controller detected (version: 0x%08x)\n", version);
        return;
    }

    uint32_t cap = mmio_read32(s_ahci_base, ahci::AHCI_CAP);
    cprintf("AHCI: version 0x%08x, CAP 0x%08x\n", version, cap);

    uint32_t ghc = mmio_read32(s_ahci_base, ahci::AHCI_GHC);
    ghc |= ahci::GHC_AHCI_EN | ahci::GHC_IE;  // Enable AHCI and interrupts
    mmio_write32(s_ahci_base, ahci::AHCI_GHC, ghc);

    uint32_t pi = mmio_read32(s_ahci_base, ahci::AHCI_PI);
    cprintf("AHCI: Ports implemented: 0x%08x\n", pi);

    // Detect devices on each port
    for (int i = 0; i < ahci::MAX_DEVICES; i++) {
        if (!(pi & (1 << i))) {
            continue;  // Port not implemented
        }

        auto& config = s_ahci_port_configs[i];
        uint32_t portBase = s_ahci_base + 0x100 + (i * 0x80);

        // Check if device is present
        uint32_t ssts = mmio_read32(portBase, ahci::PORT_SATA_STS);
        if ((ssts & ahci::SATA_STS_DET_MASK) != ahci::SATA_STS_DET_PRESENT) {
            cprintf("AHCI: Port %d: no device detected\n", i);
            continue;
        }

        cprintf("AHCI: Port %d: device detected, enabling...\n", i);

        if (ahci_enable_port(portBase) != 0) {
            cprintf("AHCI: Port %d: failed to enable\n", i);
            continue;
        }

        if (ahci_wait_port_ready(portBase, 5000) != 0) {
            cprintf("AHCI: Port %d: device not ready\n", i);
            continue;
        }

        cprintf("AHCI: Port %d: device ready\n", i);

        // Enable port interrupts
        uint32_t ie = mmio_read32(portBase, ahci::PORT_IE);
        ie |= (ahci::IS_DHRS | ahci::IS_PSS | ahci::IS_DPS | ahci::IS_UFS);
        mmio_write32(portBase, ahci::PORT_IE, ie);

        // Detect device
        s_ahci_devices[s_ahci_devices_count++].detect(&config, s_ahci_base);
    }
    
    cprintf("AHCI: Found %d device(s)\n", s_ahci_devices_count);
}

AhciDevice* AhciManager::get_device(int deviceID) {
    if (deviceID < 0 || deviceID >= s_ahci_devices_count) {
        return nullptr;
    }
    if (!s_ahci_devices[deviceID].m_present) {
        return nullptr;
    }
    return &s_ahci_devices[deviceID];
}

int AhciManager::get_device_count() {
    return s_ahci_devices_count;
}

int AhciDevice::read(uint32_t blockNumber, void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("AhciDevice::read: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("AhciDevice::read: out of range (block %d + %d > %d)\n", blockNumber, blockCount, m_info.size);
        return -1;
    }

    // Read blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        {
            InterruptsGuard guard;

            m_request.reset();
            m_request.buffer = reinterpret_cast<uint8_t*>(buf) + i * ahci::SECTOR_SIZE;
            m_request.op = AhciRequest::Op::Read;
            m_request.waiting = TaskManager::get_current();

            // In a real implementation, would set up command list and issue READ_DMA_EXT command
            // For now, just simulate success
            m_request.done = 1;
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!m_request.done) {
            {
                InterruptsGuard guard;
                if (m_request.done) break;
                TaskManager::get_current()->m_state = ProcessState::Sleeping;
            }
            TaskManager::schedule();
        }

        if (m_request.err) {
            m_request.reset();
            cprintf("AhciDevice::read: error reading block %d from %s\n", lba, m_name);
            return -1;
        }

        m_request.reset();
    }
    
    return 0;
}

int AhciDevice::write(uint32_t blockNumber, const void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("AhciDevice::write: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("AhciDevice::write: out of range (block %d + %d > %d)\n", blockNumber, blockCount, m_info.size);
        return -1;
    }

    // Write blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        {
            InterruptsGuard guard;

            m_request.reset();
            m_request.buffer = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf) + i * ahci::SECTOR_SIZE);
            m_request.op = AhciRequest::Op::Write;
            m_request.waiting = TaskManager::get_current();

            // In a real implementation, would set up command list and issue WRITE_DMA_EXT command
            // For now, just simulate success
            m_request.done = 1;
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!m_request.done) {
            {
                InterruptsGuard guard;
                if (m_request.done) break;
                TaskManager::get_current()->m_state = ProcessState::Sleeping;
            }
            TaskManager::schedule();
        }

        if (m_request.err) {
            m_request.reset();
            cprintf("AhciDevice::write: error writing block %d to %s\n", lba, m_name);
            return -1;
        }

        m_request.reset();
    }
    
    return 0;
}

/**
 * Handle AHCI interrupt for specified port
 * @param port AHCI port number
 */
void AhciManager::interrupt_handler(int port) {
    for (int i = 0; i < s_ahci_devices_count; i++) {
        AhciDevice& dev = s_ahci_devices[i];

        if (!dev.m_present || dev.m_config->port_num != port) {
            continue;
        }
        if (dev.m_request.op == AhciRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}

void AhciManager::test() {
    cprintf("AHCI: test() - AHCI controller initialized with %d device(s)\n", s_ahci_devices_count);
    for (int i = 0; i < s_ahci_devices_count; i++) {
        AhciDevice* dev = get_device(i);
        if (dev) {
            cprintf("AHCI: test() - Device %d: %s (%d sectors)\n", i, dev->m_name, dev->m_info.size);
        }
    }
}
