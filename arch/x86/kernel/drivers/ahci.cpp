#include <sys/iterator.hpp>
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

constexpr int ENGINE_POLL_LIMIT = 500000;
constexpr int COMMAND_POLL_LIMIT = 10000000;

constexpr size_t MAX_SECTORS = PG_SIZE / ahci::SECTOR_SIZE;

const pci::DriverId AHCI_IDS[] = {
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_MASS_STORAGE, pci::SUBCLASS_SATA, pci::INTERFACE_AHCI},
};

const pci::Driver AHCI_DRIVER = {
    "ahci",
    AHCI_IDS,
    static_cast<int>(sys::size(AHCI_IDS)),
    AhciManager::probe_callback,
};

}  // namespace

void AhciPrdt::set_data_buffer(uintptr_t buf_phys, uint32_t data_bytes) {
    dba = static_cast<uint32_t>(buf_phys);
    dbau = static_cast<uint32_t>(buf_phys >> 32);
    dbc = data_bytes - 1;
}

void RegisterHostToDeviceFis::set_command(uint8_t cmd, uint32_t lba, uint16_t count) {
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

Error AhciDevice::detect(const AhciPortConfig* cfg, uintptr_t mmio_base) {
    this->config = cfg;
    port_base_ = mmio_base + ahci::PORT_BASE_OFFSET + (cfg->port * ahci::PORT_REG_SIZE);

    strncpy(name, cfg->name, sizeof(name));
    TRY(setup_memory());
    TRY(identify());

    present_ = 1;
    type = blk::DeviceType::Disk;
    info.serial = cfg->port;

    return Error::None;
}

Error AhciDevice::identify() {
    TRY(issue_cmd(ahci::ATA_CMD_IDENTIFY, 0, 1, false));
    TRY(wait_cmd_complete());

    uint16_t* id = reinterpret_cast<uint16_t*>(dma_buf_);

    info.cylinders = id[1];
    info.heads = id[3];
    info.sectors = id[6];
    info.block_count = static_cast<uint32_t>(id[60]) | (static_cast<uint32_t>(id[61]) << 16);  // Total LBA28 sectors
    block_count = info.block_count;
    ENSURE(block_count != 0, Error::NoDevice);
    info.valid = 1;

    return Error::None;
}

Error AhciDevice::setup_memory() {
    uintptr_t cmd_phys = virt_to_phys(cmd_list_);
    uintptr_t fis_phys = virt_to_phys(fis_base_);
    uintptr_t table_phys = virt_to_phys(&cmd_table_);

    TRY(stop_engine());

    mmio::write32(port_base_, ahci::PORT_CLB, static_cast<uint32_t>(cmd_phys));
    mmio::write32(port_base_, ahci::PORT_CLBU, static_cast<uint32_t>(cmd_phys >> 32));

    mmio::write32(port_base_, ahci::PORT_FB, static_cast<uint32_t>(fis_phys));
    mmio::write32(port_base_, ahci::PORT_FBU, static_cast<uint32_t>(fis_phys >> 32));

    cmd_list_[0].ctba = static_cast<uint32_t>(table_phys);
    cmd_list_[0].ctbau = static_cast<uint32_t>(table_phys >> 32);

    // Clear pending interrupts
    mmio::write32(port_base_, ahci::PORT_IS, 0xFFFFFFFF);

    uint32_t cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
    cmd |= ahci::CMD_FRE;
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd);

    // Read back to flush the posted FRE write before starting commands.
    cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
    cmd |= ahci::CMD_ST;
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd);

    // Transfers are polled; an ISR must not consume their error status.
    mmio::write32(port_base_, ahci::PORT_IE, 0);
    memory_configured_ = true;

    return Error::None;
}

Error AhciDevice::stop_engine() {
    mmio::write32(port_base_, ahci::PORT_IE, 0);
    uint32_t cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd & ~ahci::CMD_ST);
    for (int polls = ENGINE_POLL_LIMIT;; --polls) {
        cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
        if ((cmd & ahci::CMD_CR) == 0) {
            break;
        }
        ENSURE(polls != 0, Error::Timeout);
        arch_spin_hint();
    }
    mmio::write32(port_base_, ahci::PORT_CMD_STAT, cmd & ~ahci::CMD_FRE);
    for (int polls = ENGINE_POLL_LIMIT;; --polls) {
        cmd = mmio::read32(port_base_, ahci::PORT_CMD_STAT);
        if ((cmd & ahci::CMD_FR) == 0) {
            break;
        }
        ENSURE(polls != 0, Error::Timeout);
        arch_spin_hint();
    }
    return Error::None;
}

Error AhciDevice::shutdown() {
    present_ = 0;
    if (port_base_ == 0) {
        return Error::None;
    }
    TRY(stop_engine());
    if (memory_configured_) {
        mmio::write32(port_base_, ahci::PORT_CLB, 0);
        mmio::write32(port_base_, ahci::PORT_CLBU, 0);
        mmio::write32(port_base_, ahci::PORT_FB, 0);
        mmio::write32(port_base_, ahci::PORT_FBU, 0);
    }
    memory_configured_ = false;
    port_base_ = 0;
    return Error::None;
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
        cprintf("ahci%d: [DEBUG] PSS (PIO Setup FIS) interrupt received. Implementation pending.\n", config->port);
    }

    if (is & ahci::IS_PCS) {
        cprintf("ahci%d: port connect change detected\n", config->port);
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
    if (registered_) {
        return 0;
    }

    cprintf("ahci: registering PCI driver...\n");

    Error registered = pci::register_driver(&AHCI_DRIVER);
    if (registered != Error::None) {
        return static_cast<int>(registered);
    }

    registered_ = true;

    return 0;
}

Error AhciManager::probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*) {
    ENSURE(pdev, Error::Invalid);
    ENSURE(!ctrl_ready_, Error::Busy);
    uint32_t bar = pci::read_bar(pdev->bus, pdev->slot, pdev->function, 5);
    ENSURE(bar != 0 && (bar & 1U) == 0, Error::Invalid);
    ENSURE((bar & 6U) == 0, Error::NotSupported);
    uintptr_t phys = bar & ~0xFU;
    ENSURE(phys != 0, Error::Invalid);
    auto mapped = vmm::map_mmio(phys, ahci::AHCI_BAR_SIZE, VM_WRITE | VM_NOCACHE);
    if (!mapped.ok()) {
        return mapped.release_error();
    }
    auto mapping = mapped.release_value();
    pci::CommandGuard command(*pdev, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER);
    uintptr_t base = mapping->address();
    uint32_t version = mmio::read32(base, ahci::AHCI_VS);
    ENSURE(version != 0 && version != 0xFFFFFFFF, Error::NoDevice);
    uint32_t original_ghc = mmio::read32(base, ahci::AHCI_GHC);
    // No interrupt handler is needed by the synchronous polling path.
    mmio::write32(base, ahci::AHCI_GHC, (original_ghc | ahci::GHC_AHCI_EN) & ~ahci::GHC_IE);
    uint32_t ports = mmio::read32(base, ahci::AHCI_PI);
    BlockDevice* candidates[ahci::MAX_DEVICES]{};
    size_t count = 0;
    Error error = Error::NoDevice;
    bool unsafe = false;
    for (int port = 0; port < ahci::MAX_DEVICES; ++port) {
        if ((ports & (1U << port)) == 0) {
            continue;
        }
        uintptr_t port_base = base + ahci::PORT_BASE_OFFSET + port * ahci::PORT_REG_SIZE;
        uint32_t status = mmio::read32(port_base, ahci::PORT_SATA_STS);
        if ((status & ahci::SATA_STS_DET_MASK) != ahci::SATA_STS_DET_PRESENT) {
            continue;
        }
        auto& device = devices_[port];
        error = device.detect(&port_configs_[port], base);
        if (error != Error::None) {
            Error stopped = device.shutdown();
            if (stopped != Error::None) {
                unsafe = true;
                break;
            }
            cprintf("ahci: port %d setup failed: %s\n", port, error_str(error));
            continue;
        }
        candidates[count++] = &device;
    }
    if (!unsafe && count != 0) {
        error = BlockManager::register_devices(candidates, count);
        if (error == Error::None) {
            for (size_t i = 0; i < count; ++i) {
                assert(published_.try_push_back(static_cast<AhciDevice*>(candidates[i])));
            }
            mapping_ = sys::move(mapping);
            ctrl_ready_ = true;
            command.commit();
            return Error::None;
        }
    }
    // No candidate has been published: stop all staged DMA before rollback.
    for (auto& device : devices_) {
        Error stopped = device.shutdown();
        if (stopped != Error::None) {
            unsafe = true;
            cprintf("ahci: port shutdown failed: %s; retaining controller resources\n", error_str(stopped));
        }
    }
    if (unsafe) {
        mapping_ = sys::move(mapping);
        ctrl_ready_ = true;  // Quarantined; never reuse potentially live DMA buffers.
        command.commit();
    } else {
        mmio::write32(base, ahci::AHCI_GHC, original_ghc);
    }
    return error;
}

AhciDevice* AhciManager::find_device(int index) {
    if (index < 0 || static_cast<size_t>(index) >= published_.size()) {
        return nullptr;
    }
    if (!published_[index]->present_) {
        return nullptr;
    }
    return published_[index];
}

int AhciManager::device_count() {
    return static_cast<int>(published_.size());
}

void AhciDevice::print_info() {
    cprintf("Device: %s (AHCI port %d)\n", name, config->port);
    cprintf("  Size: %d sectors (%d MB)\n", info.block_count, info.block_count / 2048);
    cprintf("  CHS: %d/%d/%d\n", info.cylinders, info.heads, info.sectors);
    cprintf("\n");
}

Error AhciDevice::transfer_blocks(uint32_t start_lba, size_t block_count, void* buf, bool write) {
    const char* op_name = write ? "write" : "read";

    if (!present_) {
        cprintf("AhciDevice::%s: device %s not present\n", op_name, name);
        return Error::NoDevice;
    }

    ENSURE(buf || block_count == 0, Error::Invalid);
    if (start_lba > info.block_count || block_count > info.block_count - start_lba) {
        cprintf("AhciDevice::%s: out of range (block %d + %d > %d)\n", op_name, start_lba, block_count,
                info.block_count);
        return Error::Invalid;
    }

    uint8_t* data = static_cast<uint8_t*>(buf);
    size_t remaining = block_count;
    uint32_t lba = start_lba;

    const uint8_t command = write ? ahci::ATA_CMD_WRITE_DMA_EXT : ahci::ATA_CMD_READ_DMA_EXT;

    while (remaining > 0) {
        size_t count = (remaining > MAX_SECTORS) ? MAX_SECTORS : remaining;
        size_t bytes = count * ahci::SECTOR_SIZE;

        if (write) {
            memcpy(dma_buf_, data, bytes);
        }

        TRY(issue_cmd(command, lba, count, write));
        Error completed = wait_cmd_complete();
        if (completed != Error::None) {
            // Stop timed-out DMA before the caller can submit another command.
            Error stopped = shutdown();
            if (stopped != Error::None) {
                cprintf("ahci: %s: failed to stop DMA: %s\n", name, error_str(stopped));
            }
            return completed;
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

Error AhciDevice::read(uint32_t start_lba, void* buf, size_t block_count) {
    return transfer_blocks(start_lba, block_count, buf, false);
}

Error AhciDevice::write(uint32_t start_lba, const void* buf, size_t block_count) {
    return transfer_blocks(start_lba, block_count, const_cast<void*>(buf), true);
}

Error AhciDevice::issue_cmd(uint8_t command, uint32_t lba, uint16_t count, bool write) {
    if (count == 0 || count > MAX_SECTORS) {
        return Error::Invalid;
    }

    for (int polls_left = 100000; polls_left > 0; --polls_left) {
        uint32_t tfd = mmio::read32(port_base_, ahci::PORT_TFD);
        if ((tfd & (ahci::TFD_STS_BSY | ahci::TFD_STS_DRQ)) == 0) {
            break;
        }
        if (polls_left == 1) {
            return Error::Timeout;
        }
        arch_spin_hint();
    }

    if (mmio::read32(port_base_, ahci::PORT_CI) & 1) {
        return Error::Busy;
    }

    const uintptr_t buf_phys = virt_to_phys(dma_buf_);
    const uint32_t data_bytes = static_cast<uint32_t>(count) * ahci::SECTOR_SIZE;

    auto& cmd = cmd_list_[0];
    cmd.cfl = sizeof(RegisterHostToDeviceFis) / 4;
    cmd.write = write ? 1 : 0;
    cmd.prdtl = 1;
    cmd.prdbc = 0;

    cmd_table_ = {};

    auto* fis = reinterpret_cast<RegisterHostToDeviceFis*>(cmd_table_.cfis);
    fis->set_command(command, lba, count);

    auto& prdt = cmd_table_.prdt[0];
    prdt.set_data_buffer(buf_phys, data_bytes);

    // Clear port interrupt status
    mmio::write32(port_base_, ahci::PORT_IS, 0xFFFFFFFF);
    // Issue command (slot 0)
    mmio::write32(port_base_, ahci::PORT_CI, 1);

    return Error::None;
}

Error AhciDevice::wait_cmd_complete() const {
    for (int polls = COMMAND_POLL_LIMIT; polls > 0; --polls) {
        uint32_t status = mmio::read32(port_base_, ahci::PORT_IS);
        uint32_t tfd = mmio::read32(port_base_, ahci::PORT_TFD);
        ENSURE((status & (ahci::IS_OFS | ahci::IS_TFES | ahci::IS_UFS)) == 0 && (tfd & ahci::TFD_STS_ERR) == 0,
               Error::Io);
        if ((mmio::read32(port_base_, ahci::PORT_CI) & 1U) == 0) {
            return Error::None;
        }
        arch_spin_hint();
    }
    return Error::Timeout;
}

void AhciManager::interrupt_handler(int port) {
    for (auto* device : published_) {
        AhciDevice& dev = *device;

        if (!dev.present_ || dev.config->port != port) {
            continue;
        }
        if (dev.request.op == AhciRequest::Op::None) {
            continue;
        }

        dev.interrupt();
    }
}
