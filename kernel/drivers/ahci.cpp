#include "ahci.h"
#include "pci.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/memory.h"

#include <asm/arch.h>
#include <asm/page.h>
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

void AhciDevice::detect(const AhciPortConfig* cfg, uintptr_t mmio_base) {
    this->config = cfg;
    port_base = mmio_base + ahci::PORT_BASE_OFFSET + (cfg->port_num * ahci::PORT_REG_SIZE);

    type = blk::DeviceType::Disk;
    present = 1;

    info.serial = cfg->port_num;
    info.model = 0;
    info.valid = 0;

    strncpy(name, cfg->name, sizeof(name));

    // Setup DMA memory for this port
    setup_memory();
    if (!present)
        return;  // setup_memory failed

    // Send IDENTIFY to get real disk geometry
    identify();
}

void AhciDevice::identify() {
    memset(dma_buf, 0, ahci::SECTOR_SIZE);

    if (issue_cmd(ahci::ATA_CMD_IDENTIFY, 0, 0, false) != 0) {
        cprintf("AHCI: %s: IDENTIFY failed to issue\n", name);
        info.size = 0;
        return;
    }

    if (wait_cmd_complete(1000) != 0) {
        cprintf("AHCI: %s: IDENTIFY timeout\n", name);
        info.size = 0;
        return;
    }

    uint16_t* id = reinterpret_cast<uint16_t*>(dma_buf);

    info.cylinders = id[1];
    info.heads = id[3];
    info.sectors = id[6];
    info.size = *reinterpret_cast<uint32_t*>(&id[60]);  // Total LBA28 sectors
    info.valid = 1;
}

void AhciDevice::setup_memory() {
    // Allocate DMA memory for AHCI structures
    cmd_list = reinterpret_cast<AhciCmdHeader*>(kmalloc(PG_SIZE));  // Command List (1KB needed, allocate page)
    fis_base = reinterpret_cast<uint8_t*>(kmalloc(PG_SIZE));        // Received FIS (256B needed)
    cmd_table = reinterpret_cast<AhciCmdTable*>(kmalloc(PG_SIZE));  // Command Table
    dma_buf = reinterpret_cast<uint8_t*>(kmalloc(PG_SIZE));         // DMA Buffer

    if (!cmd_list || !fis_base || !cmd_table || !dma_buf) {
        cprintf("AHCI: Failed to allocate DMA memory for port %d\n", config->port_num);
        present = 0;
        return;
    }

    // Get physical addresses for hardware registers
    uintptr_t cmd_phys = virt_to_phys(cmd_list);
    uintptr_t fis_phys = virt_to_phys(fis_base);
    uintptr_t table_phys = virt_to_phys(cmd_table);

    // Zero out the memory
    memset(cmd_list, 0, PG_SIZE);
    memset(fis_base, 0, PG_SIZE);
    memset(cmd_table, 0, PG_SIZE);
    memset(dma_buf, 0, PG_SIZE);

    // Stop the port before configuring
    uint32_t cmd = AhciManager::mmio_read32(port_base, ahci::PORT_CMD_STAT);
    cmd &= ~(ahci::CMD_ST | ahci::CMD_FRE);
    AhciManager::mmio_write32(port_base, ahci::PORT_CMD_STAT, cmd);

    // Wait for port to stop
    for (int i = 0; i < 500000; i++) {
        cmd = AhciManager::mmio_read32(port_base, ahci::PORT_CMD_STAT);
        if (!(cmd & (ahci::CMD_CR | ahci::CMD_FR))) {
            break;
        }
    }

    // Set command list base address
    AhciManager::mmio_write32(port_base, ahci::PORT_CLB, cmd_phys & 0xFFFFFFFF);
    AhciManager::mmio_write32(port_base, ahci::PORT_CLBU, 0);

    // Set FIS base address
    AhciManager::mmio_write32(port_base, ahci::PORT_FB, fis_phys & 0xFFFFFFFF);
    AhciManager::mmio_write32(port_base, ahci::PORT_FBU, 0);

    // Setup command header 0 to point to command table
    cmd_list[0].ctba = table_phys & 0xFFFFFFFF;
    cmd_list[0].ctbau = 0;

    // Clear pending interrupts
    AhciManager::mmio_write32(port_base, ahci::PORT_IS, 0xFFFFFFFF);

    // Start the port
    cmd = AhciManager::mmio_read32(port_base, ahci::PORT_CMD_STAT);
    cmd |= ahci::CMD_FRE;
    AhciManager::mmio_write32(port_base, ahci::PORT_CMD_STAT, cmd);

    // Wait a bit
    for (int i = 0; i < 100000; i++) {}

    cmd |= ahci::CMD_ST;
    AhciManager::mmio_write32(port_base, ahci::PORT_CMD_STAT, cmd);

    // Enable port interrupts
    uint32_t ie = AhciManager::mmio_read32(port_base, ahci::PORT_IE);
    ie |= (ahci::IS_DHRS | ahci::IS_PSS | ahci::IS_DPS | ahci::IS_UFS);
    AhciManager::mmio_write32(port_base, ahci::PORT_IE, ie);
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

    s_base = vmm::mmio_map(phys_base, ahci::AHCI_BAR_SIZE, VM_WRITE | VM_NOCACHE);
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

        cprintf("AHCI: Port %d: device detected, initializing...\n", i);

        // Detect and setup device (setup_memory will configure the port)
        s_devices[s_devices_count++].detect(&config, s_base);

        if (!s_devices[s_devices_count - 1].present) {
            cprintf("AHCI: Port %d: failed to setup\n", i);
            s_devices_count--;
            continue;
        }

        cprintf("AHCI: Port %d: device ready\n", i);
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

void AhciDevice::print_info() {
    cprintf("Device: %s (AHCI port %d)\n", name, config->port_num);
    cprintf("  Size: %d sectors (%d MB)\n", info.size, info.size / 2048);
    cprintf("  CHS: %d/%d/%d\n", info.cylinders, info.heads, info.sectors);
    cprintf("\n");
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

    // Max sectors per transfer (limited by DMA buffer size: 4KB = 8 sectors)
    constexpr size_t MAX_SECTORS = PG_SIZE / ahci::SECTOR_SIZE;
    uint8_t* dest = reinterpret_cast<uint8_t*>(buf);
    size_t remaining = block_count;
    uint32_t lba = block_number;

    while (remaining > 0) {
        size_t count = (remaining > MAX_SECTORS) ? MAX_SECTORS : remaining;

        if (issue_cmd(ahci::ATA_CMD_READ_DMA_EXT, lba, count, false) != 0) {
            cprintf("AhciDevice::read: failed to issue command for LBA %d\n", lba);
            return -1;
        }

        if (wait_cmd_complete(1000) != 0) {
            cprintf("AhciDevice::read: timeout reading LBA %d from %s\n", lba, name);
            return -1;
        }

        memcpy(dest, dma_buf, count * ahci::SECTOR_SIZE);
        dest += count * ahci::SECTOR_SIZE;
        lba += count;
        remaining -= count;
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

    constexpr size_t MAX_SECTORS = PG_SIZE / ahci::SECTOR_SIZE;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(buf);
    size_t remaining = block_count;
    uint32_t lba = block_number;

    while (remaining > 0) {
        size_t count = (remaining > MAX_SECTORS) ? MAX_SECTORS : remaining;

        memcpy(dma_buf, src, count * ahci::SECTOR_SIZE);

        if (issue_cmd(ahci::ATA_CMD_WRITE_DMA_EXT, lba, count, true) != 0) {
            cprintf("AhciDevice::write: failed to issue command for LBA %d\n", lba);
            return -1;
        }

        if (wait_cmd_complete(1000) != 0) {
            cprintf("AhciDevice::write: timeout writing LBA %d to %s\n", lba, name);
            return -1;
        }

        src += count * ahci::SECTOR_SIZE;
        lba += count;
        remaining -= count;
    }

    return 0;
}

int AhciDevice::issue_cmd(uint8_t command, uint32_t lba, uint16_t count, bool write) {
    // Wait for port to be ready
    uint32_t tfd = AhciManager::mmio_read32(port_base, ahci::PORT_TFD);
    int timeout = 100000;
    while ((tfd & (ahci::TFD_STS_BSY | ahci::TFD_STS_DRQ)) && timeout-- > 0) {
        tfd = AhciManager::mmio_read32(port_base, ahci::PORT_TFD);
    }
    if (timeout <= 0) {
        return -1;
    }

    // Get physical address of DMA buffer
    uintptr_t buf_phys = virt_to_phys(dma_buf);

    // Setup command header
    cmd_list[0].cfl = sizeof(FisRegH2D) / 4;  // Command FIS length in DWORDs
    cmd_list[0].write = write ? 1 : 0;
    cmd_list[0].prdtl = 1;  // 1 PRDT entry
    cmd_list[0].prdbc = 0;

    // Setup command table
    memset(cmd_table, 0, sizeof(AhciCmdTable));

    // Build command FIS (Register H2D)
    FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_table->cfis);
    fis->fis_type = ahci::FIS_TYPE_REG_H2D;
    fis->c = 1;  // This is a command
    fis->command = command;
    fis->device = 0x40;  // LBA mode

    // Set LBA (48-bit)
    fis->lba0 = lba & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->lba4 = 0;
    fis->lba5 = 0;

    // Set sector count
    fis->countl = count & 0xFF;
    fis->counth = (count >> 8) & 0xFF;

    // Setup PRDT entry
    cmd_table->prdt[0].dba = buf_phys & 0xFFFFFFFF;
    cmd_table->prdt[0].dbau = 0;
    cmd_table->prdt[0].dbc = (count * ahci::SECTOR_SIZE - 1) | 0;  // Byte count - 1

    // Clear port interrupt status
    AhciManager::mmio_write32(port_base, ahci::PORT_IS, 0xFFFFFFFF);

    // Issue command (slot 0)
    AhciManager::mmio_write32(port_base, ahci::PORT_CI, 1);

    return 0;
}

int AhciDevice::wait_cmd_complete(int timeout_ms) {
    int timeout = timeout_ms * 10000;  // Rough iteration count

    while (timeout-- > 0) {
        uint32_t ci = AhciManager::mmio_read32(port_base, ahci::PORT_CI);
        if ((ci & 1) == 0) {
            // Command completed
            uint32_t is = AhciManager::mmio_read32(port_base, ahci::PORT_IS);
            if (is & ahci::IS_OFS) {
                // Overflow error
                return -1;
            }
            return 0;
        }

        // Check for errors
        uint32_t tfd = AhciManager::mmio_read32(port_base, ahci::PORT_TFD);
        if (tfd & ahci::TFD_STS_ERR) {
            return -1;
        }
    }

    return -1;  // Timeout
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
