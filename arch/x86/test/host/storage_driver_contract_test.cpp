#include "drivers/ahci.h"
#include "drivers/ide.h"
#include "drivers/sdhci.h"
#include "drivers/pci.h"
#include "drivers/mmio.h"
#include "drivers/intr.h"
#include "mm/vmm.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdarg.h"
#include <asm/mmu.h>
#include <sys/iterator.hpp>

extern "C" int vprintf(const char*, va_list);
extern "C" void* aligned_alloc(size_t, size_t);
extern "C" void free(void*);
extern "C" [[noreturn]] void exit(int);

class HardwareFixture {
public:
    enum class Backend { Ahci, Sd, Ide };
    enum class Fault { None, Reset, Clock, Cr, Fr, Io, Timeout };
    struct State {
        Backend backend{Backend::Ahci};
        Fault fault{};
        uint32_t registers[2048]{};
        pte_t leaves[256]{};
        pde_t root[PAGE_TABLE_ENTRIES]{};
        int pte_budget{-1};
        bool fail_allocation{};
        int allocations{};
        int dma_address_writes{};
        int transfers{};
        int sd_command{};
        uint16_t command{1};
        uint32_t bar{0x100008};
        bool echoed_status{};
        uint8_t control[2]{};
        bool primary_master{true};
        uint8_t ide_command{};
        bool fail_stop_after_command{};
    };
    static State& state() { return state_; }

    static size_t index(uintptr_t address) { return (address - KERNEL_DEVIO_BASE) / 4; }
    static uint32_t& reg(uintptr_t address) {
        assert(index(address) < sys::size(state().registers));
        return state().registers[index(address)];
    }
    static int mapped_pages() {
        int count = 0;
        for (auto entry : state().leaves) {
            count += pte_present(entry);
        }
        return count;
    }
    static void ahci() {
        state().registers[ahci::AHCI_VS / 4] = 0x00010000;
        state().registers[ahci::AHCI_PI / 4] = 3;
        for (int port = 0; port < 2; ++port) {
            state().registers[(ahci::PORT_BASE_OFFSET + port * ahci::PORT_REG_SIZE + ahci::PORT_SATA_STS) / 4] = 3;
        }
    }

private:
    static State state_;
};

HardwareFixture::State HardwareFixture::state_{};

int cprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
void panic_at(const char* file, int line, const char* format, ...) {
    cprintf("assertion at %s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    exit(77);
}
void* operator new(size_t size, const sys::nothrow_t&) noexcept {
    if (HardwareFixture::state().fail_allocation) {
        HardwareFixture::state().fail_allocation = false;
        return nullptr;
    }
    void* pointer = aligned_alloc(4096, (size + 4095) / 4096 * 4096);
    assert(pointer);
    ++HardwareFixture::state().allocations;
    return pointer;
}
void operator delete(void* pointer) noexcept {
    if (pointer) {
        --HardwareFixture::state().allocations;
        free(pointer);
    }
}
void operator delete(void* pointer, size_t) noexcept {
    operator delete(pointer);
}

intr::Guard::Guard() : flag_(0) {}
intr::Guard::~Guard() = default;
namespace storage_host {
void arch_invalidate_tlb_range(uintptr_t, size_t) {}
}  // namespace storage_host
void pmm::free_user_pgdir(pde_t*) {}
pte_t* pmm::get_pte(pde_t*, uintptr_t va, bool create) {
    if (create && HardwareFixture::state().pte_budget == 0) {
        return nullptr;
    }
    if (create && HardwareFixture::state().pte_budget > 0) {
        --HardwareFixture::state().pte_budget;
    }
    size_t index = (va - KERNEL_DEVIO_BASE) / PG_SIZE;
    assert(index < sys::size(HardwareFixture::state().leaves));
    return &HardwareFixture::state().leaves[index];
}
void pmm::unmap_mmio_page(pde_t* root, uintptr_t va) {
    *get_pte(root, va, false) = 0;
}
void WaitQueue::wakeup_one() {}
namespace i8259 {
void enable(unsigned int) {}
}  // namespace i8259

namespace pci {
int bus_count() {
    return 0;
}
uint32_t config_read32(int, int, int, int offset) {
    return offset == Command                       ? 0xFFFF0000U | HardwareFixture::state().command
           : offset >= Bar0 && offset <= Bar0 + 20 ? 0x100000
                                                   : 0;
}
void config_write32(int, int, int, int offset, uint32_t value) {
    if (offset == Command) {
        HardwareFixture::state().echoed_status |= (value >> 16) != 0;
        HardwareFixture::state().command = static_cast<uint16_t>(value);
    }
}
}  // namespace pci

namespace storage_host {
uint32_t read32(uintptr_t address) {
    using Fixture = HardwareFixture;
    if (Fixture::state().backend == Fixture::Backend::Sd) {
        uintptr_t offset = address - KERNEL_DEVIO_BASE;
        if (offset == 0x24) {
            return 0;  // No command/data inhibit.
        }
        if (offset == 0x10) {
            switch (Fixture::state().sd_command) {
                case 8: return 0x1AA;
                case 41: return 0xC0000000;
                case 3: return 0x00010000;
                default: return 0;
            }
        }
        if (offset == 0x1C && Fixture::state().sd_command == 9) {
            return 1U << 22;  // CSD v2, 1024 sectors.
        }
    }
    return Fixture::reg(address);
}
uint16_t read16(uintptr_t address) {
    using Fixture = HardwareFixture;
    uintptr_t offset = address - KERNEL_DEVIO_BASE;
    if (Fixture::state().backend == Fixture::Backend::Sd) {
        if (offset == 0x2C) {
            return Fixture::state().fault == Fixture::Fault::Clock ? 0 : 3;
        }
        if (offset == 0x30) {
            return Fixture::state().fault == Fixture::Fault::Io        ? 0x8000
                   : Fixture::state().fault == Fixture::Fault::Timeout ? 0
                                                                       : 0x33;
        }
    }
    return static_cast<uint16_t>(read32(address & ~uintptr_t(3)) >> ((address & 3) * 8));
}
uint8_t read8(uintptr_t address) {
    if (HardwareFixture::state().backend == HardwareFixture::Backend::Sd && address - KERNEL_DEVIO_BASE == 0x2F) {
        return HardwareFixture::state().fault == HardwareFixture::Fault::Reset ? 1 : 0;
    }
    return static_cast<uint8_t>(read32(address & ~uintptr_t(3)) >> ((address & 3) * 8));
}
uint64_t read64(uintptr_t address) {
    return read32(address) | (static_cast<uint64_t>(read32(address + 4)) << 32);
}

void write32(uintptr_t address, uint32_t value) {
    using Fixture = HardwareFixture;
    uintptr_t offset = address - KERNEL_DEVIO_BASE;
    if (Fixture::state().backend == Fixture::Backend::Ahci && offset >= ahci::PORT_BASE_OFFSET) {
        uintptr_t port = offset - (offset - ahci::PORT_BASE_OFFSET) % ahci::PORT_REG_SIZE;
        uintptr_t field = offset - port;
        if (field == ahci::PORT_CMD_STAT) {
            value &= ~(ahci::CMD_CR | ahci::CMD_FR);
            if ((value & ahci::CMD_ST) != 0 || Fixture::state().fault == Fixture::Fault::Cr) {
                value |= ahci::CMD_CR;
            }
            if ((value & ahci::CMD_CR) == 0) {
                Fixture::state().registers[(port + ahci::PORT_CI) / 4] = 0;
            }
            if ((value & ahci::CMD_FRE) != 0 || Fixture::state().fault == Fixture::Fault::Fr) {
                value |= ahci::CMD_FR;
            }
        }
        if (field == ahci::PORT_CLB || field == ahci::PORT_CLBU || field == ahci::PORT_FB || field == ahci::PORT_FBU) {
            ++Fixture::state().dma_address_writes;
            assert((Fixture::state().registers[(port + ahci::PORT_CMD_STAT) / 4] & (ahci::CMD_CR | ahci::CMD_FR)) == 0);
        }
        if (field == ahci::PORT_IS) {
            Fixture::reg(address) &= ~value;
            return;
        }
        if (field == ahci::PORT_CI && value != 0) {
            if (Fixture::state().fail_stop_after_command) {
                Fixture::state().fault = Fixture::Fault::Cr;
                Fixture::state().registers[(port + ahci::PORT_IS) / 4] |= ahci::IS_TFES;
            } else if (Fixture::state().fault == Fixture::Fault::Io) {
                Fixture::state().registers[(port + ahci::PORT_IS) / 4] |= ahci::IS_TFES;
            } else if (Fixture::state().fault != Fixture::Fault::Timeout) {
                uintptr_t clb =
                    Fixture::state().registers[(port + ahci::PORT_CLB) / 4] |
                    (static_cast<uintptr_t>(Fixture::state().registers[(port + ahci::PORT_CLBU) / 4]) << 32);
                auto* header = phys_to_virt<AhciCmdHeader>(clb);
                auto* table = phys_to_virt<AhciCmdTable>(header->ctba | (static_cast<uintptr_t>(header->ctbau) << 32));
                auto* data =
                    phys_to_virt<uint16_t>(table->prdt[0].dba | (static_cast<uintptr_t>(table->prdt[0].dbau) << 32));
                memset(data, 0, 512);
                data[60] = 128;
                value = 0;
                ++Fixture::state().transfers;
            }
        }
    }
    Fixture::reg(address) = value;
}
void write16(uintptr_t address, uint16_t value) {
    if (HardwareFixture::state().backend == HardwareFixture::Backend::Sd && address - KERNEL_DEVIO_BASE == 0x0E) {
        HardwareFixture::state().sd_command = value >> 8;
        return;
    }
    auto& word = HardwareFixture::reg(address & ~uintptr_t(3));
    unsigned int shift = (address & 3) * 8;
    word = (word & ~(0xFFFFU << shift)) | (static_cast<uint32_t>(value) << shift);
}
void write8(uintptr_t address, uint8_t value) {
    auto& word = HardwareFixture::reg(address & ~uintptr_t(3));
    unsigned int shift = (address & 3) * 8;
    word = (word & ~(0xFFU << shift)) | (static_cast<uint32_t>(value) << shift);
}
void write64(uintptr_t address, uint64_t value) {
    write32(address, value);
    write32(address + 4, value >> 32);
}
}  // namespace storage_host

namespace storage_host {
uint8_t arch_port_read8(uint16_t port) {
    using Fixture = HardwareFixture;
    if ((port & ~7U) != ide::IDE0_BASE || !Fixture::state().primary_master) {
        return 0;
    }
    if (Fixture::state().ide_command != 0 && Fixture::state().fault == Fixture::Fault::Io) {
        return ide::STATUS_DRDY | ide::STATUS_ERR;
    }
    if (Fixture::state().ide_command != 0 && Fixture::state().fault == Fixture::Fault::Timeout) {
        return ide::STATUS_BSY;
    }
    return ide::STATUS_DRDY | ide::STATUS_DRQ;
}
void arch_port_write8(uint16_t port, uint8_t value) {
    if (port == ide::IDE0_CTRL || port == ide::IDE1_CTRL) {
        HardwareFixture::state().control[port == ide::IDE1_CTRL] = value;
    }
    if (port == ide::IDE0_BASE + ide::REG_DEVICE) {
        HardwareFixture::state().primary_master = (value & 0x10) == 0;
        HardwareFixture::state().ide_command = 0;
    }
    if (port == ide::IDE0_BASE + ide::REG_COMMAND) {
        HardwareFixture::state().ide_command = value;
    }
}
void arch_port_read16_buffer(uint16_t, void* buffer, size_t count) {
    memset(buffer, 0, count * 2);
    auto* words = static_cast<uint16_t*>(buffer);
    words[60] = 128;
    ++HardwareFixture::state().transfers;
}
void arch_port_write16_buffer(uint16_t, void*, size_t) {
    ++HardwareFixture::state().transfers;
}

}  // namespace storage_host

class DummyDevice : public BlockDevice {
public:
    Error read(uint32_t, void*, size_t) override { return Error::None; }
    Error write(uint32_t, const void*, size_t) override { return Error::None; }
};

static void fill_registry() {
    static DummyDevice devices[BlockManager::MAX_DEVICES];
    for (auto& device : devices) {
        assert(blk::register_device(&device) == Error::None);
    }
}

static void test_mmio() {
    auto invalid = vmm::map_mmio(0, 0, VM_WRITE);
    assert(!invalid.ok() && invalid.error() == Error::Invalid);
    auto overflow = vmm::map_mmio(static_cast<uintptr_t>(-1) - 1, 4, VM_WRITE);
    assert(!overflow.ok() && overflow.error() == Error::Invalid);
    HardwareFixture::state().fail_allocation = true;
    auto no_memory = vmm::map_mmio(0x1000, PG_SIZE, VM_WRITE);
    assert(!no_memory.ok() && no_memory.error() == Error::NoMem);
    HardwareFixture::state().pte_budget = 1;
    auto partial = vmm::map_mmio(0x1000, PG_SIZE * 2, VM_WRITE);
    assert(!partial.ok() && partial.error() == Error::NoMem);
    assert(HardwareFixture::mapped_pages() == 0 && HardwareFixture::state().allocations == 0);
    HardwareFixture::state().pte_budget = -1;
    auto first = vmm::map_mmio(0x1003, PG_SIZE, VM_WRITE);
    assert(first.ok());
    auto owner = first.release_value();
    assert(owner->address() == KERNEL_DEVIO_BASE + 3 && owner->size() == PG_SIZE);
    assert(HardwareFixture::mapped_pages() == 2);
    auto second = vmm::map_mmio(0x8000, PG_SIZE, VM_WRITE);
    assert(second.ok());
    auto survivor = second.release_value();
    assert(survivor->address() == KERNEL_DEVIO_BASE + PG_SIZE * 2);
    auto moved = sys::move(owner);
    assert(!owner);
    moved.reset();
    auto retry = vmm::map_mmio(0x9000, PG_SIZE, VM_WRITE);
    assert(retry.ok() && retry.value()->address() == KERNEL_DEVIO_BASE);
    retry.release_value().reset();
    survivor.reset();
    assert(HardwareFixture::mapped_pages() == 0 && HardwareFixture::state().allocations == 0);
    // Permanent boot mappings share reservations with releasable owners.
    assert(vmm::mmio_map(0xA000, PG_SIZE, VM_WRITE) == KERNEL_DEVIO_BASE);
    auto temporary = vmm::map_mmio(0xB000, PG_SIZE, VM_WRITE);
    assert(temporary.ok() && temporary.value()->address() == KERNEL_DEVIO_BASE + PG_SIZE);
    temporary.release_value().reset();
    assert(vmm::mmio_map(0xC000, PG_SIZE, VM_WRITE) == KERNEL_DEVIO_BASE + PG_SIZE);
    assert(HardwareFixture::mapped_pages() == 2 && HardwareFixture::state().allocations == 2);
}

static void test_ahci(const char* test) {
    using Fixture = HardwareFixture;
    Fixture::ahci();
    pci::DeviceInfo pci_device{};
    Error expected = Error::None;
    if (strcmp(test, "ahci_dma_stop") == 0) {
        Fixture::state().fail_stop_after_command = true;
        expected = Error::Io;
    }
    if (strcmp(test, "ahci_map") == 0) {
        Fixture::state().pte_budget = 0;
        expected = Error::NoMem;
    }
    if (strcmp(test, "ahci_version") == 0) {
        Fixture::state().registers[ahci::AHCI_VS / 4] = 0;
        expected = Error::NoDevice;
    }
    if (strcmp(test, "ahci_cr") == 0) {
        Fixture::state().fault = Fixture::Fault::Cr;
        expected = Error::Timeout;
    }
    if (strcmp(test, "ahci_fr") == 0) {
        Fixture::state().fault = Fixture::Fault::Fr;
        expected = Error::Timeout;
    }
    if (strcmp(test, "ahci_io") == 0 || strcmp(test, "ahci_retry") == 0) {
        Fixture::state().fault = Fixture::Fault::Io;
        expected = Error::Io;
    }
    if (strcmp(test, "ahci_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::Timeout;
        expected = Error::Timeout;
    }
    if (strcmp(test, "ahci_full") == 0) {
        fill_registry();
        expected = Error::Full;
    }
    Error result = AhciManager::probe_callback(&pci_device, nullptr);
    assert(result == expected);
    bool quarantined = Fixture::state().fault == Fixture::Fault::Cr || Fixture::state().fault == Fixture::Fault::Fr;
    if (quarantined) {
        assert(AhciManager::device_count() == 0);
        assert(Fixture::state().fail_stop_after_command ? Fixture::state().dma_address_writes != 0
                                                        : Fixture::state().dma_address_writes == 0);
        assert(Fixture::mapped_pages() != 0);
        assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::Busy);
        return;
    }
    if (expected != Error::None) {
        assert(AhciManager::device_count() == 0 && Fixture::mapped_pages() == 0);
        assert(Fixture::state().allocations == 0 && Fixture::state().command == 1);
        if (strcmp(test, "ahci_retry") != 0) {
            return;
        }
        Fixture::state().fault = Fixture::Fault::None;
        assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::None);
    }
    assert(AhciManager::device_count() == 2 && BlockManager::device_count() == 2);
    assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::Busy);
    auto* device = AhciManager::find_device(0);
    uint8_t buffer[513]{};
    assert(device->read(0, nullptr, 1) == Error::Invalid);
    assert(device->read(1, buffer, static_cast<size_t>(-1)) == Error::Invalid);
    assert(device->read(128, nullptr, 0) == Error::None);
    if (strcmp(test, "ahci_transfer_io") == 0) {
        Fixture::state().fault = Fixture::Fault::Io;
    }
    if (strcmp(test, "ahci_transfer_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::Timeout;
    }
    Error transfer = device->read(0, buffer + 1, 1);
    if (Fixture::state().fault != Fixture::Fault::None) {
        assert(transfer == (Fixture::state().fault == Fixture::Fault::Io ? Error::Io : Error::Timeout));
        assert(device->read(0, buffer, 1) == Error::NoDevice);
    } else {
        assert(transfer == Error::None && device->write(0, buffer + 1, 1) == Error::None);
    }
}

static void test_sd(const char* test) {
    using Fixture = HardwareFixture;
    Fixture::state().backend = Fixture::Backend::Sd;
    pci::DeviceInfo pci_device{};
    Error expected = Error::None;
    if (strcmp(test, "sd_map") == 0) {
        Fixture::state().pte_budget = 0;
        expected = Error::NoMem;
    }
    if (strcmp(test, "sd_reset") == 0) {
        Fixture::state().fault = Fixture::Fault::Reset;
        expected = Error::Timeout;
    }
    if (strcmp(test, "sd_clock") == 0) {
        Fixture::state().fault = Fixture::Fault::Clock;
        expected = Error::Timeout;
    }
    if (strcmp(test, "sd_io") == 0 || strcmp(test, "sd_retry") == 0) {
        Fixture::state().fault = Fixture::Fault::Io;
        expected = Error::Io;
    }
    if (strcmp(test, "sd_full") == 0) {
        fill_registry();
        expected = Error::Full;
    }
    assert(sdhci::Manager::probe_callback(&pci_device, nullptr) == expected);
    if (expected != Error::None) {
        assert(sdhci::device_count() == 0 && Fixture::mapped_pages() == 0);
        assert(Fixture::state().allocations == 0 && Fixture::state().command == 1);
        assert(Fixture::state().registers[0x28 / 4] == 0);  // Power is off.
        if (strcmp(test, "sd_retry") != 0) {
            return;
        }
        Fixture::state().fault = Fixture::Fault::None;
        assert(sdhci::Manager::probe_callback(&pci_device, nullptr) == Error::None);
    }
    assert(sdhci::device_count() == 1 && BlockManager::device_count() == 1);
    auto* device = sdhci::find_device();
    uint8_t buffer[513]{};
    assert(device->read(0, nullptr, 1) == Error::Invalid);
    assert(device->read(1, buffer, static_cast<size_t>(-1)) == Error::Invalid);
    assert(device->read(0, buffer + 1, 1) == Error::None);
    assert(device->write(0, buffer + 1, 1) == Error::None);
    Fixture::state().fault = Fixture::Fault::Io;
    assert(device->read(0, buffer, 1) == Error::Io);
    Fixture::state().fault = Fixture::Fault::Timeout;
    assert(device->write(0, buffer, 1) == Error::Timeout);
}

static void test_ide(const char* test) {
    using Fixture = HardwareFixture;
    Fixture::state().backend = Fixture::Backend::Ide;
    if (strcmp(test, "ide_full") == 0) {
        fill_registry();
        assert(IdeManager::init() == Error::Full);
        assert(IdeManager::device_count() == 0 && Fixture::state().control[0] == 0);
        assert(IdeManager::init() == Error::Full);
        return;
    }
    if (strcmp(test, "ide_identify_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::Timeout;
        assert(IdeManager::init() == Error::Timeout);
        assert(IdeManager::device_count() == 0 && Fixture::state().control[0] == 0);
        Fixture::state().fault = Fixture::Fault::None;
    }
    assert(IdeManager::init() == Error::None);
    assert(IdeManager::device_count() == 1);
    assert(IdeManager::init() == Error::None && BlockManager::device_count() == 1);
    auto* device = IdeManager::find_device(0);
    uint8_t buffer[512]{};
    assert(device->read(0, nullptr, 1) == Error::Invalid);
    assert(device->write(1, buffer, static_cast<size_t>(-1)) == Error::Invalid);
    if (strcmp(test, "ide_io") == 0) {
        Fixture::state().fault = Fixture::Fault::Io;
    }
    if (strcmp(test, "ide_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::Timeout;
    }
    Error expected = Fixture::state().fault == Fixture::Fault::Io        ? Error::Io
                     : Fixture::state().fault == Fixture::Fault::Timeout ? Error::Timeout
                                                                         : Error::None;
    assert(device->read(0, buffer, 1) == expected);
    assert(Fixture::state().control[0] == 0);
    assert(device->write(0, buffer, 1) == expected);
    assert(Fixture::state().control[0] == 0);
}

extern "C" int main(int argc, char** argv) {
    assert(argc == 2);
    // This is a host-only mock Manager, backed by a local fixture root. It does
    // not borrow assembly boot tables or the running kernel address space.
    vmm::Manager::kernel_mm().pgdir = HardwareFixture::state().root;
    const char* test = argv[1];
    if (strcmp(test, "mmio") == 0) {
        test_mmio();
    } else if (strcmp(test, "pci") == 0) {
        pci::DeviceInfo device{};
        {
            pci::CommandGuard guard(device, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER);
            assert(HardwareFixture::state().command == 7);
        }
        assert(HardwareFixture::state().command == 1);
        {
            pci::CommandGuard guard(device, pci::CMD_MEMORY_SPACE);
            guard.commit();
        }
        assert(HardwareFixture::state().command == 3 && !HardwareFixture::state().echoed_status);
    } else if (strcmp(test, "registry") == 0) {
        DummyDevice devices[5];
        assert(blk::register_device(nullptr) == Error::Invalid);
        assert(blk::register_device(&devices[0]) == Error::None);
        assert(blk::register_device(&devices[0]) == Error::Exists);
        assert(blk::register_device(&devices[1]) == Error::None);
        assert(blk::register_device(&devices[2]) == Error::None);
        BlockDevice* batch[]{&devices[3], &devices[4]};
        assert(BlockManager::register_devices(batch, 2) == Error::Full && BlockManager::device_count() == 3);
        assert(blk::register_device(&devices[3]) == Error::None);
        assert(blk::register_device(&devices[4]) == Error::Full && BlockManager::find_device(4) == nullptr);
    } else if (sys::strncmp(test, "ahci_", 5) == 0) {
        test_ahci(test);
    } else if (sys::strncmp(test, "sd_", 3) == 0) {
        test_sd(test);
    } else {
        test_ide(test);
    }
    assert(!HardwareFixture::state().echoed_status);
    cprintf("storage contract passed: %s\n", test);
    return 0;
}
