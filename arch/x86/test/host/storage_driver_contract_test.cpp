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
#include "storage_concurrency_test.h"

extern "C" int vprintf(const char*, va_list);
extern "C" void* aligned_alloc(size_t, size_t);
extern "C" void free(void*);
extern "C" [[noreturn]] void exit(int);

class HardwareFixture {
public:
    enum class Backend { Ahci, Sd, Ide };
    enum class Fault { None, Reset, Clock, Cr, Fr, Io, Timeout, TaskBusy };
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
        int sd_command[2]{};
        uint16_t command{1};
        uint32_t bar{0x100008};
        bool echoed_status{};
        uint8_t control[2]{};
        uint8_t ide_drives[2]{};
        bool all_ide_drives{};
        bool secondary_atapi{};
        uint8_t atapi_error{ide::ERROR_ABORTED};
        uint8_t atapi_status{ide::STATUS_DRDY | ide::STATUS_ERR};
        bool ata_aborted{};
        uint8_t ide_commands[2]{};
        uint32_t ide_lba[2]{};
        bool fail_stop_after_command{};
        size_t accesses{};
        AhciDevice* initializing_ahci{};
        SdDevice* initializing_sd{};
        bool initializing_controller{};
        bool reenter_probe{};
    };
    static State& state() { return state_; }

    static void touch_hardware() {
        StorageConcurrency::hardware_access();
        __atomic_add_fetch(&state_.accesses, static_cast<size_t>(1), __ATOMIC_RELAXED);
        if (state_.initializing_ahci) {
            assert(state_.initializing_ahci->state() == blk::DeviceState::Initializing);
        }
        if (state_.initializing_sd) {
            assert(state_.initializing_sd->state() == blk::DeviceState::Initializing);
        }
        if (state_.initializing_controller) {
            assert(AhciManager::state() == blk::DeviceState::Initializing);
        }
        if (state_.reenter_probe) {
            state_.reenter_probe = false;
            pci::DeviceInfo device{};
            assert(AhciManager::probe_callback(&device, nullptr) == Error::Busy);
        }
    }

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

// The host fixture substitutes the scheduling backend, just as it substitutes
// MMIO and interrupt masking. Real blocking Mutex semantics run in sync_test.
void Mutex::lock() {
    StorageConcurrency::before_lock(this);
    while (__atomic_exchange_n(&held_, true, __ATOMIC_ACQUIRE)) {
        sched_yield();
    }
    StorageConcurrency::acquired();
}
void Mutex::unlock() {
    StorageConcurrency::releasing();
    __atomic_store_n(&held_, false, __ATOMIC_RELEASE);
}
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
    Fixture::touch_hardware();
    if (Fixture::state().backend == Fixture::Backend::Ahci && Fixture::state().fault == Fixture::Fault::TaskBusy &&
        address - KERNEL_DEVIO_BASE >= ahci::PORT_BASE_OFFSET &&
        (address - KERNEL_DEVIO_BASE - ahci::PORT_BASE_OFFSET) % ahci::PORT_REG_SIZE == ahci::PORT_TFD) {
        return ahci::TFD_STS_BSY;
    }
    if (Fixture::state().backend == Fixture::Backend::Sd) {
        size_t controller = (address - KERNEL_DEVIO_BASE) / PG_SIZE;
        uintptr_t offset = (address - KERNEL_DEVIO_BASE) % PG_SIZE;
        if (offset == 0x24) {
            return 0;  // No command/data inhibit.
        }
        if (offset == 0x10) {
            switch (Fixture::state().sd_command[controller]) {
                case 8: return 0x1AA;
                case 41: return 0xC0000000;
                case 3: return 0x00010000;
                default: return 0;
            }
        }
        if (offset == 0x1C && Fixture::state().sd_command[controller] == 9) {
            return 1U << 22;  // CSD v2, 1024 sectors.
        }
        if (offset == 0x20 && StorageConcurrency::active()) {
            uint32_t lba = Fixture::state().registers[(controller * PG_SIZE + 0x08) / 4];
            return StorageConcurrency::pattern(lba) * 0x01010101U;
        }
    }
    return Fixture::reg(address);
}
uint16_t read16(uintptr_t address) {
    using Fixture = HardwareFixture;
    Fixture::touch_hardware();
    uintptr_t offset = (address - KERNEL_DEVIO_BASE) % PG_SIZE;
    if (Fixture::state().backend == Fixture::Backend::Sd) {
        if (offset == 0x2C) {
            return Fixture::state().fault == Fixture::Fault::Clock ? 0 : 3;
        }
        if (offset == 0x30) {
            if (StorageConcurrency::command_error() == Error::Io) {
                return 0x8000;
            }
            if (StorageConcurrency::command_error() == Error::Timeout) {
                return 0;
            }
            return Fixture::state().fault == Fixture::Fault::Io        ? 0x8000
                   : Fixture::state().fault == Fixture::Fault::Timeout ? 0
                                                                       : 0x33;
        }
    }
    return static_cast<uint16_t>(read32(address & ~uintptr_t(3)) >> ((address & 3) * 8));
}
uint8_t read8(uintptr_t address) {
    HardwareFixture::touch_hardware();
    if (HardwareFixture::state().backend == HardwareFixture::Backend::Sd &&
        (address - KERNEL_DEVIO_BASE) % PG_SIZE == 0x2F) {
        return HardwareFixture::state().fault == HardwareFixture::Fault::Reset ? 1 : 0;
    }
    return static_cast<uint8_t>(read32(address & ~uintptr_t(3)) >> ((address & 3) * 8));
}
uint64_t read64(uintptr_t address) {
    return read32(address) | (static_cast<uint64_t>(read32(address + 4)) << 32);
}

void write32(uintptr_t address, uint32_t value) {
    using Fixture = HardwareFixture;
    Fixture::touch_hardware();
    uintptr_t offset = address - KERNEL_DEVIO_BASE;
    if (Fixture::state().backend == Fixture::Backend::Sd && offset % PG_SIZE == 0x20 && StorageConcurrency::active()) {
        size_t controller = offset / PG_SIZE;
        uint32_t lba = Fixture::state().registers[(controller * PG_SIZE + 0x08) / 4];
        assert(value == StorageConcurrency::pattern(lba) * 0x01010101U);
        return;
    }
    if (Fixture::state().backend == Fixture::Backend::Ahci && offset >= ahci::PORT_BASE_OFFSET) {
        uintptr_t port = offset - (offset - ahci::PORT_BASE_OFFSET) % ahci::PORT_REG_SIZE;
        uintptr_t field = offset - port;
        if (field == ahci::PORT_CMD_STAT) {
            value &= ~(ahci::CMD_CR | ahci::CMD_FR);
            if ((value & ahci::CMD_ST) != 0 || Fixture::state().fault == Fixture::Fault::Cr ||
                StorageConcurrency::quarantine()) {
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
                if (StorageConcurrency::active()) {
                    auto* fis = reinterpret_cast<RegisterHostToDeviceFis*>(table->cfis);
                    uint32_t lba = fis->lba0 | (static_cast<uint32_t>(fis->lba1) << 8) |
                                   (static_cast<uint32_t>(fis->lba2) << 16) | (static_cast<uint32_t>(fis->lba3) << 24);
                    size_t blocks = fis->countl | (static_cast<size_t>(fis->counth) << 8);
                    StorageConcurrency::command((port - ahci::PORT_BASE_OFFSET) / ahci::PORT_REG_SIZE, lba, blocks);
                    assert(table->prdt[0].dbc + 1 == blocks * 512);
                    if (StorageConcurrency::command_error() != Error::None) {
                        if (StorageConcurrency::command_error() == Error::Io) {
                            Fixture::state().registers[(port + ahci::PORT_IS) / 4] |= ahci::IS_TFES;
                        }
                        Fixture::reg(address) = value;
                        return;
                    }
                    auto* bytes = reinterpret_cast<uint8_t*>(data);
                    for (size_t sector = 0; sector < blocks; ++sector) {
                        if (header->write) {
                            for (size_t byte = 0; byte < 512; ++byte) {
                                assert(bytes[sector * 512 + byte] == StorageConcurrency::pattern(lba + sector));
                            }
                        } else {
                            memset(bytes + sector * 512, StorageConcurrency::pattern(lba + sector), 512);
                        }
                    }
                } else {
                    memset(data, 0, 512);
                    data[60] = 128;
                }
                value = 0;
                __atomic_add_fetch(&Fixture::state().transfers, 1, __ATOMIC_RELAXED);
            }
        }
    }
    Fixture::reg(address) = value;
}
void write16(uintptr_t address, uint16_t value) {
    HardwareFixture::touch_hardware();
    if (HardwareFixture::state().backend == HardwareFixture::Backend::Sd &&
        (address - KERNEL_DEVIO_BASE) % PG_SIZE == 0x0E) {
        size_t controller = (address - KERNEL_DEVIO_BASE) / PG_SIZE;
        HardwareFixture::state().sd_command[controller] = value >> 8;
        if (StorageConcurrency::active()) {
            assert((value >> 8) == 17 || (value >> 8) == 24);
            uint32_t lba = HardwareFixture::state().registers[(controller * PG_SIZE + 0x08) / 4];
            StorageConcurrency::command(controller, lba, 1);
        }
        return;
    }
    auto& word = HardwareFixture::reg(address & ~uintptr_t(3));
    unsigned int shift = (address & 3) * 8;
    word = (word & ~(0xFFFFU << shift)) | (static_cast<uint32_t>(value) << shift);
}
void write8(uintptr_t address, uint8_t value) {
    HardwareFixture::touch_hardware();
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
    Fixture::touch_hardware();
    int channel = (port & ~7U) == ide::IDE1_BASE;
    int field = port & 7U;
    bool atapi = Fixture::state().secondary_atapi && channel == 1 && Fixture::state().ide_drives[channel] == 0;
    if (!Fixture::state().all_ide_drives && !atapi && (channel != 0 || Fixture::state().ide_drives[channel] != 0)) {
        return 0;
    }
    if (field == ide::REG_LBA_MID || field == ide::REG_LBA_HIGH) {
        return static_cast<uint8_t>(Fixture::state().ide_lba[channel] >> ((field - ide::REG_LBA_LOW) * 8));
    }
    if (field == ide::REG_ERROR) {
        return atapi ? Fixture::state().atapi_error : Fixture::state().ata_aborted ? ide::ERROR_ABORTED : 0;
    }
    if (atapi && Fixture::state().ide_commands[channel] == ide::CMD_IDENTIFY) {
        return Fixture::state().atapi_status;
    }
    if (Fixture::state().ata_aborted && Fixture::state().ide_commands[channel] == ide::CMD_IDENTIFY) {
        return ide::STATUS_DRDY | ide::STATUS_ERR;
    }
    if (Fixture::state().ide_commands[channel] != 0 &&
        (Fixture::state().fault == Fixture::Fault::Io || StorageConcurrency::command_error() == Error::Io)) {
        return ide::STATUS_DRDY | ide::STATUS_ERR;
    }
    if (Fixture::state().ide_commands[channel] != 0 &&
        (Fixture::state().fault == Fixture::Fault::Timeout || StorageConcurrency::command_error() == Error::Timeout)) {
        return ide::STATUS_BSY;
    }
    return ide::STATUS_DRDY | ide::STATUS_DRQ;
}
void arch_port_write8(uint16_t port, uint8_t value) {
    HardwareFixture::touch_hardware();
    int channel = (port & ~7U) == ide::IDE1_BASE;
    int field = port & 7U;
    if (field == ide::REG_DEVICE && port != ide::IDE0_CTRL && port != ide::IDE1_CTRL) {
        HardwareFixture::state().ide_commands[channel] = 0;
        HardwareFixture::state().ide_drives[channel] = (value & 0x10) != 0;
    }
    if (field >= ide::REG_LBA_LOW && field <= ide::REG_LBA_HIGH && port != ide::IDE0_CTRL && port != ide::IDE1_CTRL) {
        unsigned int shift = (field - ide::REG_LBA_LOW) * 8;
        auto& lba = HardwareFixture::state().ide_lba[channel];
        lba = (lba & ~(0xFFU << shift)) | (static_cast<uint32_t>(value) << shift);
    }
    if (field == ide::REG_COMMAND) {
        HardwareFixture::state().ide_commands[channel] = value;
        if (value == ide::CMD_IDENTIFY && channel == 1 && HardwareFixture::state().secondary_atapi &&
            HardwareFixture::state().ide_drives[channel] == 0) {
            HardwareFixture::state().ide_lba[channel] = (static_cast<uint32_t>(ide::ATAPI_SIGNATURE_MID) << 8) |
                                                        (static_cast<uint32_t>(ide::ATAPI_SIGNATURE_HIGH) << 16);
        }
        if (StorageConcurrency::active()) {
            assert(value == ide::CMD_READ || value == ide::CMD_WRITE);
            StorageConcurrency::command(channel, HardwareFixture::state().ide_lba[channel], 1);
        }
    }
    if (port == ide::IDE0_CTRL || port == ide::IDE1_CTRL) {
        HardwareFixture::state().control[port == ide::IDE1_CTRL] = value;
    }
}
void arch_port_read16_buffer(uint16_t port, void* buffer, size_t count) {
    HardwareFixture::touch_hardware();
    if (StorageConcurrency::active()) {
        int channel = port == ide::IDE1_BASE;
        memset(buffer, StorageConcurrency::pattern(HardwareFixture::state().ide_lba[channel]), count * 2);
        return;
    }
    memset(buffer, 0, count * 2);
    auto* words = static_cast<uint16_t*>(buffer);
    words[60] = 128;
    ++HardwareFixture::state().transfers;
}
void arch_port_write16_buffer(uint16_t port, void* buffer, size_t count) {
    HardwareFixture::touch_hardware();
    if (StorageConcurrency::active()) {
        int channel = port == ide::IDE1_BASE;
        auto* bytes = static_cast<uint8_t*>(buffer);
        for (size_t byte = 0; byte < count * 2; ++byte) {
            assert(bytes[byte] == StorageConcurrency::pattern(HardwareFixture::state().ide_lba[channel]));
        }
        return;
    }
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

static void test_concurrent_requests(BlockDevice* first, BlockDevice* second, int first_resource, int second_resource,
                                     size_t blocks, bool independent) {
    StorageConcurrency::run(first, second, first_resource, second_resource, blocks, false, false, independent);
    StorageConcurrency::run(first, second, first_resource, second_resource, blocks, true, false, independent);
    StorageConcurrency::run(first, second, first_resource, second_resource, blocks, false, true, independent);
    StorageConcurrency::run(first, second, first_resource, second_resource, blocks, true, true, independent);
}

template<class Device>
static void test_concurrent_failure(Device* first, Device* second, int second_resource, const char* test, size_t blocks,
                                    size_t chunk) {
    bool independent = sys::strstr(test, "_independent_") != nullptr;
    bool first_write = sys::strstr(test, "_wr_") != nullptr || sys::strstr(test, "_ww_") != nullptr;
    bool second_write = sys::strstr(test, "_rw_") != nullptr || sys::strstr(test, "_ww_") != nullptr;
    bool quarantine = sys::strstr(test, "_quarantine_") != nullptr;
    StorageConcurrency::Failure failure{
        sys::strstr(test, "_timeout_") != nullptr ? Error::Timeout : Error::Io,
        sys::strstr(test, "_partial") != nullptr ? chunk : 0,
        quarantine,
    };
    StorageConcurrency::run(first, second, 0, second_resource, blocks, first_write, second_write, independent, failure);
    auto expected = quarantine ? blk::DeviceState::Quarantined : blk::DeviceState::Offline;
    assert(first->state() == expected);
    if (first != second) {
        assert(second->state() == blk::DeviceState::Ready);
    }
    uint8_t buffer[512]{};
    size_t accesses = HardwareFixture::state().accesses;
    assert(first->read(0, buffer, 1) == Error::NoDevice);
    assert(first->write(0, buffer, 1) == Error::NoDevice);
    assert(HardwareFixture::state().accesses == accesses);
    if (first != second) {
        assert(second->read(0, buffer, 1) == Error::None);
    }
}

static void test_queued_failure(const char* test) {
    using Fixture = HardwareFixture;
    bool independent = sys::strstr(test, "_independent_") != nullptr;
    bool sibling = sys::strstr(test, "_sibling_") != nullptr;
    pci::DeviceInfo pci_device{};
    if (sys::strstr(test, "_ahci_") != nullptr) {
        Fixture::ahci();
        assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::None);
        int mapped_pages = Fixture::mapped_pages();
        assert(mapped_pages != 0);
        auto* first = AhciManager::find_device(0);
        test_concurrent_failure(first, independent ? AhciManager::find_device(1) : first, independent ? 1 : 0, test, 9,
                                8);
        assert(AhciManager::find_device(0) == nullptr && AhciManager::find_device(1));
        assert(AhciManager::device_count() == 2 && BlockManager::device_count() == 2);
        assert(Fixture::mapped_pages() == mapped_pages && Fixture::state().allocations == 1);
        if (first->state() == blk::DeviceState::Quarantined) {
            size_t accesses = Fixture::state().accesses;
            assert(first->shutdown() == Error::Busy);
            assert(Fixture::state().accesses == accesses);
            assert(Fixture::state().registers[(ahci::PORT_BASE_OFFSET + ahci::PORT_CLB) / 4] != 0);
        }
    } else if (sys::strstr(test, "_sd_") != nullptr) {
        Fixture::state().backend = Fixture::Backend::Sd;
        assert(sdhci::Manager::probe_callback(&pci_device, nullptr) == Error::None);
        pci_device.slot = 1;
        assert(sdhci::Manager::probe_callback(&pci_device, nullptr) == Error::None);
        auto* first = sdhci::find_device(0);
        test_concurrent_failure(first, independent ? sdhci::find_device(1) : first, independent ? 1 : 0, test, 2, 1);
        assert(sdhci::find_device(0) == nullptr && sdhci::find_device(1));
        assert(sdhci::device_count() == 2 && BlockManager::device_count() == 2);
        assert(Fixture::mapped_pages() == 2 && Fixture::state().allocations == 2);
        assert(Fixture::state().registers[0x28 / 4] == 0);
    } else {
        Fixture::state().backend = Fixture::Backend::Ide;
        Fixture::state().all_ide_drives = true;
        assert(IdeManager::init() == Error::None);
        auto* first = IdeManager::find_device(0);
        auto* second = independent ? IdeManager::find_device(2) : sibling ? IdeManager::find_device(1) : first;
        test_concurrent_failure(first, second, independent ? 1 : 0, test, 2, 1);
        assert(IdeManager::find_device(0) == nullptr && IdeManager::find_device(1) && IdeManager::find_device(2));
        assert(IdeManager::device_count() == 4 && BlockManager::device_count() == 4);
        assert(Fixture::state().control[0] == 0 && Fixture::state().control[1] == 0);
    }
}

static void test_ahci(const char* test) {
    using Fixture = HardwareFixture;
    Fixture::ahci();
    assert(AhciManager::state() == blk::DeviceState::Offline);
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
    Fixture::state().initializing_controller = true;
    Fixture::state().reenter_probe = true;
    Error result = AhciManager::probe_callback(&pci_device, nullptr);
    Fixture::state().initializing_controller = false;
    Fixture::state().reenter_probe = false;
    assert(result == expected);
    bool quarantined = Fixture::state().fault == Fixture::Fault::Cr || Fixture::state().fault == Fixture::Fault::Fr;
    if (quarantined) {
        assert(AhciManager::state() == blk::DeviceState::Quarantined);
        assert(AhciManager::device_count() == 0);
        assert(Fixture::state().fail_stop_after_command ? Fixture::state().dma_address_writes != 0
                                                        : Fixture::state().dma_address_writes == 0);
        assert(Fixture::mapped_pages() != 0);
        assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::Busy);
        return;
    }
    if (expected != Error::None) {
        assert(AhciManager::state() == blk::DeviceState::Offline);
        assert(AhciManager::device_count() == 0 && Fixture::mapped_pages() == 0);
        assert(Fixture::state().allocations == 0 && Fixture::state().command == 1);
        if (strcmp(test, "ahci_retry") != 0) {
            return;
        }
        Fixture::state().fault = Fixture::Fault::None;
        assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::None);
    }
    assert(AhciManager::device_count() == 2 && BlockManager::device_count() == 2);
    assert(AhciManager::state() == blk::DeviceState::Ready);
    assert(AhciManager::probe_callback(&pci_device, nullptr) == Error::Busy);
    auto* device = AhciManager::find_device(0);
    assert(device && device->state() == blk::DeviceState::Ready);
    if (strcmp(test, "ahci_concurrent") == 0 || strcmp(test, "ahci_independent") == 0) {
        bool independent = strcmp(test, "ahci_independent") == 0;
        test_concurrent_requests(device, independent ? AhciManager::find_device(1) : device, 0, independent ? 1 : 0, 9,
                                 independent);
        return;
    }
    uint8_t buffer[513]{};
    assert(device->read(0, nullptr, 1) == Error::Invalid);
    assert(device->read(1, buffer, static_cast<size_t>(-1)) == Error::Invalid);
    assert(device->read(128, nullptr, 0) == Error::None);
    assert(device->state() == blk::DeviceState::Ready);
    if (strcmp(test, "ahci_transfer_io") == 0) {
        Fixture::state().fault = Fixture::Fault::Io;
    }
    if (strcmp(test, "ahci_transfer_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::Timeout;
    }
    if (strcmp(test, "ahci_issue_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::TaskBusy;
    }
    if (strcmp(test, "ahci_transfer_quarantine") == 0) {
        Fixture::state().fail_stop_after_command = true;
    }
    Error transfer = device->read(0, buffer + 1, 1);
    if (Fixture::state().fault != Fixture::Fault::None) {
        bool unsafe = strcmp(test, "ahci_transfer_quarantine") == 0;
        assert(transfer == (Fixture::state().fault == Fixture::Fault::Io || unsafe ? Error::Io : Error::Timeout));
        assert(device->state() == (unsafe ? blk::DeviceState::Quarantined : blk::DeviceState::Offline));
        assert(AhciManager::find_device(0) == nullptr);
        size_t accesses = Fixture::state().accesses;
        assert(device->read(0, buffer, 1) == Error::NoDevice);
        assert(device->write(0, buffer, 1) == Error::NoDevice);
        assert(Fixture::state().accesses == accesses);
        if (unsafe) {
            AhciPortConfig config{0, IRQ_IDE1, "test"};
            assert(device->detect(&config, KERNEL_DEVIO_BASE) == Error::Busy);
            assert(device->shutdown() == Error::Busy);
            assert(Fixture::state().accesses == accesses);
            assert(Fixture::mapped_pages() != 0 && BlockManager::device_count() == 2);
        }
        Fixture::state().fault = Fixture::Fault::None;
        Fixture::state().fail_stop_after_command = false;
        assert(AhciManager::find_device(1)->read(0, buffer, 1) == Error::None);
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
    assert(device && device->state() == blk::DeviceState::Ready);
    size_t allocations = Fixture::state().allocations;
    size_t accesses = Fixture::state().accesses;
    assert(sdhci::Manager::probe_callback(&pci_device, nullptr) == Error::Busy);
    assert(Fixture::state().allocations == allocations && Fixture::state().accesses == accesses);
    if (strcmp(test, "sd_concurrent") == 0) {
        test_concurrent_requests(device, device, 0, 0, 2, false);
        return;
    }
    if (strcmp(test, "sd_independent") == 0) {
        SdDevice second;
        assert(second.init(reinterpret_cast<volatile uint8_t*>(KERNEL_DEVIO_BASE + PG_SIZE), 1) == Error::None);
        test_concurrent_requests(device, &second, 0, 1, 2, true);
        second.shutdown();
        return;
    }
    uint8_t buffer[513]{};
    assert(device->read(0, nullptr, 1) == Error::Invalid);
    assert(device->read(1, buffer, static_cast<size_t>(-1)) == Error::Invalid);
    assert(device->state() == blk::DeviceState::Ready);
    assert(device->read(0, buffer + 1, 1) == Error::None);
    assert(device->write(0, buffer + 1, 1) == Error::None);
    Fixture::state().fault = Fixture::Fault::Io;
    assert(device->read(0, buffer, 1) == Error::Io);
    assert(device->state() == blk::DeviceState::Offline && sdhci::find_device() == nullptr);
    accesses = Fixture::state().accesses;
    assert(device->write(0, buffer, 1) == Error::NoDevice);
    assert(Fixture::state().accesses == accesses);
    Fixture::state().fault = Fixture::Fault::None;
    assert(device->init(reinterpret_cast<volatile uint8_t*>(KERNEL_DEVIO_BASE), 0) == Error::None);
    Fixture::state().fault = Fixture::Fault::Timeout;
    assert(device->write(0, buffer, 1) == Error::Timeout);
    Fixture::state().fault = Fixture::Fault::None;
    assert(device->read(0, buffer, 1) == Error::NoDevice);
    assert(device->init(reinterpret_cast<volatile uint8_t*>(KERNEL_DEVIO_BASE), 0) == Error::None);
    assert(device->read(0, buffer, 1) == Error::None);  // Explicit reset can restore this stable object.
}

static void test_ide(const char* test) {
    using Fixture = HardwareFixture;
    Fixture::state().backend = Fixture::Backend::Ide;
    if (sys::strncmp(test, "ide_atapi", 9) == 0) {
        Fixture::state().secondary_atapi = true;
        Error expected = Error::None;
        int count = 1;
        if (strcmp(test, "ide_atapi_ata_io") == 0) {
            Fixture::state().fault = Fixture::Fault::Io;
            expected = Error::Io;
            count = 0;
        }
        if (strcmp(test, "ide_atapi_io") == 0) {
            Fixture::state().atapi_error = 0x40;  // UNC is a genuine media error.
            expected = Error::Io;
        }
        if (strcmp(test, "ide_atapi_df") == 0) {
            Fixture::state().atapi_status |= ide::STATUS_DF;
            expected = Error::Io;
        }
        if (strcmp(test, "ide_atapi_timeout") == 0) {
            Fixture::state().atapi_status = ide::STATUS_BSY;
            expected = Error::Timeout;
        }
        assert(IdeManager::init() == expected && IdeManager::device_count() == count);
        assert(BlockManager::device_count() == count && Fixture::state().transfers == count);
        assert(Fixture::state().control[0] == 0 && Fixture::state().control[1] == 0);
        return;
    }
    if (strcmp(test, "ide_identify_abrt") == 0 || strcmp(test, "ide_identify_stale") == 0) {
        Fixture::state().ata_aborted = true;
        if (strcmp(test, "ide_identify_stale") == 0) {
            Fixture::state().ide_lba[0] = (static_cast<uint32_t>(ide::ATAPI_SIGNATURE_MID) << 8) |
                                          (static_cast<uint32_t>(ide::ATAPI_SIGNATURE_HIGH) << 16);
        }
        assert(IdeManager::init() == Error::Io && IdeManager::device_count() == 0);
        assert(Fixture::state().control[0] == 0 && Fixture::state().transfers == 0);
        return;
    }
    if (strcmp(test, "ide_concurrent") == 0 || strcmp(test, "ide_independent") == 0 || strcmp(test, "ide_same") == 0) {
        Fixture::state().all_ide_drives = true;
        assert(IdeManager::init() == Error::None && IdeManager::device_count() == 4);
        bool independent = strcmp(test, "ide_independent") == 0;
        int second = independent ? 2 : strcmp(test, "ide_same") == 0 ? 0 : 1;
        test_concurrent_requests(IdeManager::find_device(0), IdeManager::find_device(second), 0, independent ? 1 : 0, 2,
                                 independent);
        assert(Fixture::state().control[0] == 0 && Fixture::state().control[1] == 0);
        return;
    }
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
    assert(device && device->state() == blk::DeviceState::Ready);
    uint8_t buffer[512]{};
    assert(device->read(0, nullptr, 1) == Error::Invalid);
    assert(device->write(1, buffer, static_cast<size_t>(-1)) == Error::Invalid);
    assert(device->state() == blk::DeviceState::Ready);
    if (strcmp(test, "ide_io") == 0 || strcmp(test, "ide_write_io") == 0) {
        Fixture::state().fault = Fixture::Fault::Io;
    }
    if (strcmp(test, "ide_timeout") == 0 || strcmp(test, "ide_write_timeout") == 0) {
        Fixture::state().fault = Fixture::Fault::Timeout;
    }
    Error expected = Fixture::state().fault == Fixture::Fault::Io        ? Error::Io
                     : Fixture::state().fault == Fixture::Fault::Timeout ? Error::Timeout
                                                                         : Error::None;
    bool write_first = sys::strncmp(test, "ide_write_", 10) == 0;
    assert((write_first ? device->write(0, buffer, 1) : device->read(0, buffer, 1)) == expected);
    assert(Fixture::state().control[0] == 0);
    assert(device->write(0, buffer, 1) == (expected == Error::None ? Error::None : Error::NoDevice));
    assert(Fixture::state().control[0] == 0);
    Fixture::state().fault = Fixture::Fault::None;
    assert(device->read(0, buffer, 1) == (expected == Error::None ? Error::None : Error::NoDevice));
    if (expected != Error::None) {
        assert(device->state() == blk::DeviceState::Offline && IdeManager::find_device(0) == nullptr);
        assert(IdeManager::init() == Error::None && BlockManager::device_count() == 1);
        assert(device->state() == blk::DeviceState::Offline);  // A retry never overwrites a published object.
        size_t accesses = Fixture::state().accesses;
        assert(device->read(0, buffer, 1) == Error::NoDevice);
        assert(device->write(0, buffer, 1) == Error::NoDevice);
        assert(Fixture::state().accesses == accesses);
    }
}

static void test_default_offline() {
    AhciDevice ahci_device;
    SdDevice sd_device;
    IdeDevice ide_device;
    BlockDevice* devices[]{&ahci_device, &sd_device, &ide_device};
    assert(ahci_device.state() == blk::DeviceState::Offline);
    assert(sd_device.state() == blk::DeviceState::Offline);
    assert(ide_device.state() == blk::DeviceState::Offline);
    uint8_t buffer[512]{};
    for (auto* device : devices) {
        assert(device->read(0, buffer, 1) == Error::NoDevice);
        assert(device->write(0, buffer, 1) == Error::NoDevice);
    }
    assert(HardwareFixture::state().accesses == 0);
}

static void test_ahci_lifecycle() {
    using Fixture = HardwareFixture;
    Fixture::ahci();
    AhciDevice device;
    AhciPortConfig config{0, IRQ_IDE1, "test"};
    assert(device.detect(nullptr, KERNEL_DEVIO_BASE) == Error::Invalid);
    assert(device.state() == blk::DeviceState::Offline && Fixture::state().accesses == 0);
    Fixture::state().initializing_ahci = &device;
    assert(device.detect(&config, KERNEL_DEVIO_BASE) == Error::None);
    Fixture::state().initializing_ahci = nullptr;
    assert(device.state() == blk::DeviceState::Ready);
    size_t accesses = Fixture::state().accesses;
    assert(device.detect(&config, KERNEL_DEVIO_BASE) == Error::Busy);
    assert(Fixture::state().accesses == accesses);
    assert(device.shutdown() == Error::None && device.state() == blk::DeviceState::Offline);
    accesses = Fixture::state().accesses;
    assert(device.shutdown() == Error::None && Fixture::state().accesses == accesses);
    Fixture::state().fault = Fixture::Fault::Io;
    Fixture::state().initializing_ahci = &device;
    assert(device.detect(&config, KERNEL_DEVIO_BASE) == Error::Io);
    Fixture::state().initializing_ahci = nullptr;
    assert(device.state() == blk::DeviceState::Offline);
    assert(Fixture::state().registers[(ahci::PORT_BASE_OFFSET + ahci::PORT_CLB) / 4] == 0);
    Fixture::state().fault = Fixture::Fault::None;
    assert(device.detect(&config, KERNEL_DEVIO_BASE) == Error::None);
    uint8_t buffer[512]{};
    assert(device.read(0, buffer, 1) == Error::None);
    assert(device.shutdown() == Error::None);
}

static void test_sd_lifecycle() {
    using Fixture = HardwareFixture;
    Fixture::state().backend = Fixture::Backend::Sd;
    SdDevice device;
    auto* base = reinterpret_cast<volatile uint8_t*>(KERNEL_DEVIO_BASE);
    assert(device.init(nullptr, 0) == Error::Invalid);
    assert(device.state() == blk::DeviceState::Offline && Fixture::state().accesses == 0);
    const Fixture::Fault faults[]{Fixture::Fault::Reset, Fixture::Fault::Clock, Fixture::Fault::Io};
    for (auto fault : faults) {
        Fixture::state().fault = fault;
        Fixture::state().initializing_sd = &device;
        assert(device.init(base, 0) == (fault == Fixture::Fault::Io ? Error::Io : Error::Timeout));
        Fixture::state().initializing_sd = nullptr;
        assert(device.state() == blk::DeviceState::Offline);
        assert(Fixture::state().registers[0x28 / 4] == 0);
        Fixture::state().fault = Fixture::Fault::None;
        Fixture::state().initializing_sd = &device;
        assert(device.init(base, 0) == Error::None);
        Fixture::state().initializing_sd = nullptr;
        assert(device.state() == blk::DeviceState::Ready);
        size_t accesses = Fixture::state().accesses;
        assert(device.init(base, 0) == Error::Busy && Fixture::state().accesses == accesses);
        device.shutdown();
        assert(device.state() == blk::DeviceState::Offline);
        accesses = Fixture::state().accesses;
        device.shutdown();
        assert(Fixture::state().accesses == accesses);
    }
}

static void test_sd_published_slots() {
    using Fixture = HardwareFixture;
    Fixture::state().backend = Fixture::Backend::Sd;
    pci::DeviceInfo first{};
    pci::DeviceInfo second{};
    second.slot = 1;
    assert(sdhci::Manager::probe_callback(&first, nullptr) == Error::None);
    assert(sdhci::Manager::probe_callback(&second, nullptr) == Error::None);
    auto* device = sdhci::find_device(0);
    auto* sibling = sdhci::find_device(1);
    assert(device && sibling && device != sibling);
    uint8_t buffer[512]{};
    Fixture::state().fault = Fixture::Fault::Io;
    assert(device->write(0, buffer, 1) == Error::Io);
    assert(device->state() == blk::DeviceState::Offline);
    Fixture::state().fault = Fixture::Fault::None;
    size_t accesses = Fixture::state().accesses;
    assert(sdhci::Manager::probe_callback(&first, nullptr) == Error::Busy);
    assert(device->read(0, buffer, 1) == Error::NoDevice);
    assert(Fixture::state().accesses == accesses);
    assert(sdhci::find_device(0) == nullptr && sdhci::find_device(1) == sibling);
    assert(sdhci::device_count() == 2 && BlockManager::device_count() == 2 && Fixture::mapped_pages() == 2);
    assert(sibling->read(0, buffer, 1) == Error::None);
}

extern "C" int main(int argc, char** argv) {
    assert(argc == 2);
    // This is a host-only mock Manager, backed by a local fixture root. It does
    // not borrow assembly boot tables or the running kernel address space.
    vmm::Manager::kernel_mm().pgdir = HardwareFixture::state().root;
    const char* test = argv[1];
    if (sys::strncmp(test, "queued_", 7) == 0) {
        test_queued_failure(test);
    } else if (strcmp(test, "default_offline") == 0) {
        test_default_offline();
    } else if (strcmp(test, "ahci_lifecycle") == 0) {
        test_ahci_lifecycle();
    } else if (strcmp(test, "sd_lifecycle") == 0) {
        test_sd_lifecycle();
    } else if (strcmp(test, "sd_slots") == 0) {
        test_sd_published_slots();
    } else if (strcmp(test, "mmio") == 0) {
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
