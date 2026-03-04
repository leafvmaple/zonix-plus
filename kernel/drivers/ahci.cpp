#include "ahci.h"
#include "pci.h"
#include "lib/stdio.h"
#include "lib/string.h"

#include <asm/arch.h>
#include <asm/pg.h>
#include <asm/drivers/i8259.h>
#include "pic.h"
#include "sched/sched.h"
#include "intr.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

AhciDevice AhciManager::s_devices[ahci::MAX_DEVICES] = {};
int AhciManager::s_devices_count = 0;

uintptr_t AhciManager::s_base = 0;

AhciPortConfig AhciManager::s_port_configs[ahci::MAX_DEVICES] = {
    {0, IRQ_IDE1, "sda"},  // AHCI port 0
    {1, IRQ_IDE1, "sdb"},  // AHCI port 1
    {2, IRQ_IDE1, "sdc"},  // AHCI port 2
    {3, IRQ_IDE1, "sdd"},  // AHCI port 3
};

void AhciDevice::detect(const AhciPortConfig* config, uintptr_t mmio_base) {
    config = config;
    port_base = mmio_base + ahci::PORT_BASE_OFFSET + (config->port_num * ahci::PORT_REG_SIZE);

    type = blk::DeviceType::Disk;
    present = 1;

    info.size = 131072;  // TODO
    info.serial = config->port_num;
    info.model = 0;
    info.valid = 1;

    strncpy(name, config->name, sizeof(name));
}

void AhciDevice::interrupt() {
    uint32_t is = AhciManager::mmio_read32(port_base, ahci::PORT_IS);

    AhciManager::mmio_write32(port_base, ahci::PORT_IS, is);

    if (is & ahci::IS_DHRS) {
        if (request.op == AhciRequest::Op::Read) {
            // Data is ready in buffer
        } else if (request.op == AhciRequest::Op::Write) {
            // Write completed
        }
    }

    if (is & ahci::IS_PCS) {
        cprintf("ahci%d: port connect change detected\n", config->port_num);
    }

    if (is & ahci::IS_OFS) {
        request.err = -1;
    }

    request.done = 1;
    if (request.waiting) {
        request.waiting->wakeup();
    }
}

void AhciManager::init(void) {
    cprintf("AHCI: Initializing AHCI controller...\n");

    uint32_t phys_base = pci_find_bar();

    if (phys_base == 0) {
        cprintf("AHCI: No AHCI controller found on PCI bus\n");
        return;
    }

    s_base = vmm::mmio_map(phys_base, ahci::AHCI_BAR_SIZE, PTE_W | PTE_PCD | PTE_PWT);
    uint32_t version = mmio_read32(s_base, ahci::AHCI_VS);

    // If version is 0x00000000 or 0xFFFFFFFF, controller is not present
    if (version == 0x00000000 || version == 0xFFFFFFFF) {
        cprintf("AHCI: No AHCI controller detected (version: 0x%08x)\n", version);
        return;
    }

    uint32_t cap = mmio_read32(s_base, ahci::AHCI_CAP);
    cprintf("AHCI: version 0x%08x, CAP 0x%08x\n", version, cap);

    uint32_t ghc = mmio_read32(s_base, ahci::AHCI_GHC);
    ghc |= ahci::GHC_AHCI_EN | ahci::GHC_IE;  // Enable AHCI and interrupts
    mmio_write32(s_base, ahci::AHCI_GHC, ghc);

    uint32_t ports_impl = mmio_read32(s_base, ahci::AHCI_PI);
    cprintf("AHCI: Ports implemented: 0x%08x\n", ports_impl);

    for (int i = 0; i < ahci::MAX_DEVICES; i++) {
        if (!(ports_impl & (1 << i))) {
            continue;  // Port not implemented
        }

        auto& config = s_port_configs[i];
        uintptr_t port_base = s_base + ahci::PORT_BASE_OFFSET + (i * ahci::PORT_REG_SIZE);

        // Check if device is present
        uint32_t ssts = mmio_read32(port_base, ahci::PORT_SATA_STS);
        if ((ssts & ahci::SATA_STS_DET_MASK) != ahci::SATA_STS_DET_PRESENT) {
            cprintf("AHCI: Port %d: no device detected\n", i);
            continue;
        }

        cprintf("AHCI: Port %d: device detected, enabling...\n", i);

        if (enable_port(port_base) != 0) {
            cprintf("AHCI: Port %d: failed to enable\n", i);
            continue;
        }

        if (wait_port_ready(port_base, 5000) != 0) {
            cprintf("AHCI: Port %d: device not ready\n", i);
            continue;
        }

        cprintf("AHCI: Port %d: device ready\n", i);

        // Enable port interrupts
        uint32_t ie = mmio_read32(port_base, ahci::PORT_IE);
        ie |= (ahci::IS_DHRS | ahci::IS_PSS | ahci::IS_DPS | ahci::IS_UFS);
        mmio_write32(port_base, ahci::PORT_IE, ie);

        // Detect device
        s_devices[s_devices_count++].detect(&config, s_base);
    }

    cprintf("AHCI: Found %d device(s)\n", s_devices_count);
}

AhciDevice* AhciManager::get_device(int device_id) {
    if (device_id < 0 || device_id >= s_devices_count) {
        return nullptr;
    }
    if (!s_devices[device_id].present) {
        return nullptr;
    }
    return &s_devices[device_id];
}

int AhciManager::get_device_count() {
    return s_devices_count;
}

int AhciDevice::read(uint32_t block_number, void* buf, size_t block_count) {
    if (!present) {
        cprintf("AhciDevice::read: device %s not present\n", name);
        return -1;
    }

    if (block_number + block_count > info.size) {
        cprintf("AhciDevice::read: out of range (block %d + %d > %d)\n", block_number, block_count, info.size);
        return -1;
    }

    // Read blocks one by one (interrupt-driven)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = block_number + i;

        {
            intr::Guard guard;

            request.reset();
            request.buffer = reinterpret_cast<uint8_t*>(buf) + i * ahci::SECTOR_SIZE;
            request.op = AhciRequest::Op::Read;
            request.waiting = TaskManager::get_current();

            // In a real implementation, would set up command list and issue READ_DMA_EXT command
            // For now, just simulate success
            request.done = 1;
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!request.done) {
            {
                intr::Guard guard;
                if (request.done)
                    break;
                TaskManager::get_current()->state = ProcessState::Sleeping;
            }
            TaskManager::schedule();
        }

        if (request.err) {
            request.reset();
            cprintf("AhciDevice::read: error reading block %d from %s\n", lba, name);
            return -1;
        }

        request.reset();
    }

    return 0;
}

int AhciDevice::write(uint32_t block_number, const void* buf, size_t block_count) {
    if (!present) {
        cprintf("AhciDevice::write: device %s not present\n", name);
        return -1;
    }

    if (block_number + block_count > info.size) {
        cprintf("AhciDevice::write: out of range (block %d + %d > %d)\n", block_number, block_count, info.size);
        return -1;
    }

    // Write blocks one by one (interrupt-driven)
    for (size_t i = 0; i < block_count; i++) {
        uint32_t lba = block_number + i;

        {
            intr::Guard guard;

            request.reset();
            request.buffer = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf) + i * ahci::SECTOR_SIZE);
            request.op = AhciRequest::Op::Write;
            request.waiting = TaskManager::get_current();

            // In a real implementation, would set up command list and issue WRITE_DMA_EXT command
            // For now, just simulate success
            request.done = 1;
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!request.done) {
            {
                intr::Guard guard;
                if (request.done)
                    break;
                TaskManager::get_current()->state = ProcessState::Sleeping;
            }
            TaskManager::schedule();
        }

        if (request.err) {
            request.reset();
            cprintf("AhciDevice::write: error writing block %d to %s\n", lba, name);
            return -1;
        }

        request.reset();
    }

    return 0;
}

/**
 * Handle AHCI interrupt for specified port
 * @param port AHCI port number
 */
void AhciManager::interrupt_handler(int port) {
    for (int i = 0; i < s_devices_count; i++) {
        AhciDevice& dev = s_devices[i];

        if (!dev.present || dev.config->port_num != port) {
            continue;
        }
        if (dev.request.op == AhciRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}

void AhciManager::test() {
    cprintf("AHCI: test() - AHCI controller initialized with %d device(s)\n", s_devices_count);
    for (int i = 0; i < s_devices_count; i++) {
        AhciDevice* dev = get_device(i);
        if (dev) {
            cprintf("AHCI: test() - Device %d: %s (%d sectors)\n", i, dev->name, dev->info.size);
        }
    }
}

uint32_t AhciManager::pci_find_bar() {
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

uint32_t AhciManager::mmio_read32(uintptr_t base, uint32_t offset) {
    return *(volatile uint32_t*)(base + offset);
}

void AhciManager::mmio_write32(uintptr_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(base + offset) = value;
}

int AhciManager::wait_port_ready(uintptr_t port_base, int timeout_ms) {
    int timeout = timeout_ms * 1000;  // Convert to iterations

    while (timeout-- > 0) {
        uint32_t ssts = mmio_read32(port_base, ahci::PORT_SATA_STS);

        if ((ssts & ahci::SATA_STS_DET_MASK) == ahci::SATA_STS_DET_PRESENT) {
            return 0;
        }
    }

    return -1;
}

int AhciManager::enable_port(uintptr_t port_base) {
    uint32_t cmd = mmio_read32(port_base, ahci::PORT_CMD_STAT);
    cmd |= ahci::CMD_FRE;  // Enable FIS receive
    mmio_write32(port_base, ahci::PORT_CMD_STAT, cmd);

    for (int i = 0; i < 100000; i++)
        ;

    cmd |= ahci::CMD_ST;
    mmio_write32(port_base, ahci::PORT_CMD_STAT, cmd);

    return 0;
}
