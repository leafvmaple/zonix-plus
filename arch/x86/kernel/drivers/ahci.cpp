#include "ahci.h"
#include "drivers/pci.h"
#include "drivers/mmio.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/memory.h"

#include <asm/arch.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/drivers/i8259.h>
#include "drivers/i8259.h"
#include "sched/sched.h"
#include "drivers/intr.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

namespace {

constexpr size_t MAX_SECTORS = PG_SIZE / ahci::SECTOR_SIZE;

const pci::DriverId AHCI_IDS[] = {
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_MASS_STORAGE, pci::SUBCLASS_SATA, pci::INTERFACE_AHCI},
};

const pci::Driver AHCI_DRIVER = {
    "ahci",
    AHCI_IDS,
    static_cast<int>(array_size(AHCI_IDS)),
    AhciManager::probe_callback,
};

}  // namespace

void AhciPrdt::set_data_buffer(uintptr_t buf_phys, uint32_t data_bytes) {
    dba = static_cast<uint32_t>(buf_phys);
    dbau = static_cast<uint32_t>(buf_phys >> 32);
    dbc = data_bytes - 1;
}

void FisRegH2D::set_command(uint8_t cmd, uint32_t lba, uint16_t count) {
    fis_type = ahci::FIS_TYPE_REG_H2D;
    c = 1;

    command = cmd;

    lba0 = static_cast<uint8_t>(lba);
    lba1 = static_cast<uint8_t>(lba >> 8);
    lba2 = static_cast<uint8_t>(lba >> 16);
    lba3 = static_cast<uint8_t>(lba >> 24);
    device = 0x40;

    countl = static_cast<uint8_t>(count);
    counth = static_cast<uint8_t>(count >> 8);
}

int AhciDevice::detect(const AhciPortConfig* cfg, uintptr_t mmio_base) {
    this->config = cfg;
    port_base_ = mmio_base + ahci::PORT_BASE_OFFSET + (cfg->port_num * ahci::PORT_REG_SIZE);

    strncpy(name, cfg->name, sizeof(name));
    if (setup_memory() != 0) {
        return -1;
    }

    if (identify() != 0) {
        return -1;
    }

    present_ = 1;
    type = blk::DeviceType::Disk;
    info.serial = cfg->port_num;

    return 0;
}

int AhciDevice::identify() {
    if (issue_cmd(ahci::ATA_CMD_IDENTIFY, 0, 1, false) != 0) {
        cprintf("AHCI: %s: IDENTIFY failed to issue\n", name);
        return -1;
    }

    if (wait_cmd_complete(1000) != 0) {
        cprintf("AHCI: %s: IDENTIFY timeout\n", name);
        return -1;
    }

    uint16_t* id = reinterpret_cast<uint16_t*>(dma_buf_);

    info.cylinders = id[1];
    info.heads = id[3];
    info.sectors = id[6];
    info.size = *reinterpret_cast<uint32_t*>(&id[60]);  // Total LBA28 sectors
    size = info.size;
    info.valid = 1;

    return 0;
}

int AhciDevice::setup_memory() {
    uintptr_t cmd_phys = virt_to_phys(cmd_list_);
    uintptr_t fis_phys = virt_to_phys(fis_base_);
    uintptr_t table_phys = virt_to_phys(&cmd_table_);

    uint32_t cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
    cmd &= ~(ahci::CMD_ST | ahci::CMD_FRE);
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd);

    for (int i = 0; i < 500000; i++) {
        cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
        if (!(cmd & (ahci::CMD_CR | ahci::CMD_FR))) {
            break;
        }
    }

    mmio::write32(port_base_, ahci::PORT_CLB, static_cast<uint32_t>(cmd_phys));
    mmio::write32(port_base_, ahci::PORT_CLBU, static_cast<uint32_t>(cmd_phys >> 32));

    mmio::write32(port_base_, ahci::PORT_FB, static_cast<uint32_t>(fis_phys));
    mmio::write32(port_base_, ahci::PORT_FBU, static_cast<uint32_t>(fis_phys >> 32));

    cmd_list_[0].ctba = static_cast<uint32_t>(table_phys);
    cmd_list_[0].ctbau = static_cast<uint32_t>(table_phys >> 32);

    // Clear pending interrupts
    mmio::write32(port_base_, ahci::PORT_IS, 0xFFFFFFFF);

    cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
    cmd |= ahci::CMD_FRE;
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd);

    // Wait a bit
    for (int i = 0; i < 100000; i++) {}

    cmd |= ahci::CMD_ST;
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd);

    uint32_t ie = mmio::read32(port_base_, ahci::PORT_IE);
    ie |= (ahci::IS_DHRS | ahci::IS_PSS | ahci::IS_DPS | ahci::IS_UFS);
    mmio::write32(port_base_, ahci::PORT_IE, ie);

    return 0;
}

void AhciDevice::interrupt() {
    uint32_t is = mmio::read32(port_base_, ahci::PORT_IS);

    mmio::write32(port_base_, ahci::PORT_IS, is);

    if (is & ahci::IS_DHRS) {
        if (request.op == AhciRequest::Op::Read) {
            // Data is ready in buffer
        } else if (request.op == AhciRequest::Op::Write) {
            // Write completed
        }
    }

    if (is & ahci::IS_PSS) {
        cprintf("ahci%d: [DEBUG] PSS (PIO Setup FIS) interrupt received. Implementation pending.\n", config->port_num);
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

int AhciManager::init() {
    if (s_registered) {
        return 0;
    }

    cprintf("ahci: registering PCI driver...\n");

    if (pci::register_driver(&AHCI_DRIVER) != Error::None) {
        cprintf("ahci: failed to register PCI driver\n");
        return -1;
    }

    s_registered = true;

    return 0;
}

Error AhciManager::probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*) {
    ENSURE(!s_ctrl_ready, Error::Busy);

    uint32_t bar5 = pci::read_bar(pdev->bus, pdev->dev, pdev->func, 5);
    ENSURE_LOG(bar5 != 0 && !(bar5 & 1), Error::Invalid, "ahci: invalid BAR5 for PCI %02x:%02x.%x = 0x%08x", pdev->bus,
               pdev->dev, pdev->func, bar5);

    pci::enable_bus_master(pdev->bus, pdev->dev, pdev->func);
    uint32_t phys_base = bar5 & 0xFFFFFFF0;
    cprintf("ahci: Found controller at PCI %02x:%02x.%x, ABAR=0x%08x\n", pdev->bus, pdev->dev, pdev->func, phys_base);

    s_base = vmm::mmio_map(phys_base, ahci::AHCI_BAR_SIZE, VM_WRITE | VM_NOCACHE);
    ENSURE_LOG(s_base != 0, Error::NoMem, "ahci: failed to map MMIO region at phys=0x%08x", phys_base);

    uint32_t version = mmio::read32(s_base, ahci::AHCI_VS);
    if (version == 0x00000000 || version == 0xFFFFFFFF) {
        cprintf("ahci: controller not responding (version: 0x%08x)\n", version);
        s_base = 0;
        return Error::NoDevice;
    }

    uint32_t cap = mmio::read32(s_base, ahci::AHCI_CAP);
    cprintf("ahci: version %d.%d%d, CAP 0x%08x\n", (version >> 16) & 0xFFFF, (version >> 8) & 0xFF, version & 0xFF,
            cap);

    uint32_t ghc = mmio::read32(s_base, ahci::AHCI_GHC);
    ghc |= ahci::GHC_AHCI_EN | ahci::GHC_IE;
    mmio::write32(s_base, ahci::AHCI_GHC, ghc);

    uint32_t ports_impl = mmio::read32(s_base, ahci::AHCI_PI);
    cprintf("ahci: ports implemented: 0x%08x\n", ports_impl);

    for (int i = 0; i < ahci::MAX_DEVICES; i++) {
        if (!(ports_impl & (1 << i))) {
            continue;
        }

        auto& config = s_port_configs[i];
        uintptr_t port_base = s_base + ahci::PORT_BASE_OFFSET + (i * ahci::PORT_REG_SIZE);

        uint32_t ssts = mmio::read32(port_base, ahci::PORT_SATA_STS);
        if ((ssts & ahci::SATA_STS_DET_MASK) != ahci::SATA_STS_DET_PRESENT) {
            continue;
        }

        if (s_devices[s_devices_count].detect(&config, s_base) != 0) {
            cprintf("ahci: port %d: device setup failed\n", i);
            continue;
        }

        blk::register_device(&s_devices[s_devices_count]);

        cprintf("ahci: port %d: '%s' ready (%d sectors, %d MB)\n", i, s_devices[s_devices_count].name,
                s_devices[s_devices_count].info.size, s_devices[s_devices_count].info.size / 2048);
        s_devices_count++;
    }

    cprintf("ahci: initialization complete, %d device(s)\n", s_devices_count);
    s_ctrl_ready = true;
    return Error::None;
}

AhciDevice* AhciManager::get_device(int device_id) {
    if (device_id < 0 || device_id >= s_devices_count) {
        return nullptr;
    }
    if (!s_devices[device_id].present_) {
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

Error AhciDevice::transfer_blocks(uint32_t block_number, size_t block_count, void* buf, bool write) {
    const char* op_name = write ? "write" : "read";

    if (!present_) {
        cprintf("AhciDevice::%s: device %s not present\n", op_name, name);
        return Error::NoDevice;
    }

    if (block_number + block_count > info.size) {
        cprintf("AhciDevice::%s: out of range (block %d + %d > %d)\n", op_name, block_number, block_count, info.size);
        return Error::Invalid;
    }

    uint8_t* data = static_cast<uint8_t*>(buf);
    size_t remaining = block_count;
    uint32_t lba = block_number;

    const uint8_t command = write ? ahci::ATA_CMD_WRITE_DMA_EXT : ahci::ATA_CMD_READ_DMA_EXT;

    while (remaining > 0) {
        size_t count = (remaining > MAX_SECTORS) ? MAX_SECTORS : remaining;
        size_t bytes = count * ahci::SECTOR_SIZE;

        if (write) {
            memcpy(dma_buf_, data, bytes);
        }

        if (issue_cmd(command, lba, count, write) != 0) {
            cprintf("AhciDevice::%s: failed to issue command for LBA %d\n", op_name, lba);
            return Error::IO;
        }

        if (wait_cmd_complete(1000) != 0) {
            cprintf("AhciDevice::%s: timeout %s LBA %d %s %s\n", op_name, write ? "writing" : "reading", lba,
                    write ? "to" : "from", name);
            return Error::Timeout;
        }

        if (!write) {
            memcpy(data, dma_buf_, bytes);
        }

        data += bytes;
        lba += count;
        remaining -= count;
    }

    return Error::None;
}

Error AhciDevice::read(uint32_t block_number, void* buf, size_t block_count) {
    return transfer_blocks(block_number, block_count, buf, false);
}

Error AhciDevice::write(uint32_t block_number, const void* buf, size_t block_count) {
    return transfer_blocks(block_number, block_count, const_cast<void*>(buf), true);
}

int AhciDevice::issue_cmd(uint8_t command, uint32_t lba, uint16_t count, bool write) {
    if (count == 0 || count > MAX_SECTORS) {
        return -1;
    }

    for (int timeout = 100000; timeout > 0; --timeout) {
        uint32_t tfd = mmio::read32(port_base_, ahci::PORT_TFD);
        if ((tfd & (ahci::TFD_STS_BSY | ahci::TFD_STS_DRQ)) == 0) {
            break;
        }
        if (timeout == 1) {
            return -1;
        }
        arch_spin_hint();
    }

    if (mmio::read32(port_base_, ahci::PORT_CI) & 1) {
        return -1;
    }

    const uintptr_t buf_phys = virt_to_phys(dma_buf_);
    const uint32_t data_bytes = static_cast<uint32_t>(count) * ahci::SECTOR_SIZE;

    auto& cmd = cmd_list_[0];
    cmd.cfl = sizeof(FisRegH2D) / 4;
    cmd.write = write ? 1 : 0;
    cmd.prdtl = 1;
    cmd.prdbc = 0;

    cmd_table_ = {};

    auto* fis = reinterpret_cast<FisRegH2D*>(cmd_table_.cfis);
    fis->set_command(command, lba, count);

    auto& prdt = cmd_table_.prdt[0];
    prdt.set_data_buffer(buf_phys, data_bytes);

    // Clear port interrupt status
    mmio::write32(port_base_, ahci::PORT_IS, 0xFFFFFFFF);
    // Issue command (slot 0)
    mmio::write32(port_base_, ahci::PORT_CI, 1);

    return 0;
}

int AhciDevice::wait_cmd_complete(int timeout_ms) const {
    int timeout = timeout_ms * 10000;

    while (timeout-- > 0) {
        uint32_t ci = mmio::read32(port_base_, ahci::PORT_CI);
        if ((ci & 1) == 0) {
            uint32_t is = mmio::read32(port_base_, ahci::PORT_IS);
            if (is & ahci::IS_OFS) {
                // Overflow error
                return -1;
            }
            return 0;
        }

        uint32_t tfd = mmio::read32(port_base_, ahci::PORT_TFD);
        if (tfd & ahci::TFD_STS_ERR) {
            return -1;
        }
    }

    return -1;  // Timeout
}

void AhciManager::interrupt_handler(int port) {
    for (int i = 0; i < s_devices_count; i++) {
        AhciDevice& dev = s_devices[i];

        if (!dev.present_ || dev.config->port_num != port) {
            continue;
        }
        if (dev.request.op == AhciRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}
