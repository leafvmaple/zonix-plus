#include "pci_transport.h"
#include "split_queue.h"
#include "drivers/mmio.h"
#include <asm/arch.h>
#include <kernel/vm_flags.h>

namespace {
constexpr uint8_t STATUS_ACK = 1;
constexpr uint8_t STATUS_DRIVER = 2;
constexpr uint8_t STATUS_DRIVER_OK = 4;
constexpr uint8_t STATUS_FEATURES_OK = 8;
constexpr uint8_t CAP_COMMON = 1;
constexpr uint8_t CAP_NOTIFY = 2;
constexpr uint8_t CAP_ISR = 3;
constexpr size_t STATUS_OFFSET = 0x14;
constexpr size_t QUEUE_SELECT = 0x16;
constexpr size_t QUEUE_SIZE = 0x18;
constexpr size_t QUEUE_ENABLE = 0x1C;
constexpr size_t QUEUE_NOTIFY_OFFSET = 0x1E;
}  // namespace

namespace virtio {

Result<PciTransport::Capability> PciTransport::find_capability(const pci::DeviceInfo& device, uint8_t type) {
    auto read = [&](int offset) {
        return pci::config_read32(device.bus, device.slot, device.function, offset);
    };
    ENSURE((read(pci::Command) & (1U << 20)) != 0, Error::NotFound);
    uint8_t ptr = static_cast<uint8_t>(read(pci::CapabilitiesPointer));
    // Conventional PCI config has at most 48 aligned capability locations.
    for (int remaining = 48; ptr != 0 && remaining > 0; --remaining) {
        ENSURE(ptr >= 0x40 && (ptr & 3) == 0);
        uint32_t header = read(ptr);
        uint8_t length = static_cast<uint8_t>(header >> 16);
        if (static_cast<uint8_t>(header) == 0x09 && static_cast<uint8_t>(header >> 24) == type) {
            ENSURE(length >= (type == CAP_NOTIFY ? 20 : 16) && static_cast<unsigned>(ptr) + length <= 256);
            return Capability{static_cast<uint8_t>(read(ptr + 4)), read(ptr + 8), read(ptr + 12),
                              type == CAP_NOTIFY ? read(ptr + 16) : 0};
        }
        ptr = static_cast<uint8_t>(header >> 8);
    }
    return ptr == 0 ? Error::NotFound : Error::Invalid;
}

Result<vmm::MmioRegion> PciTransport::map_capability(const pci::DeviceInfo& device, const Capability& cap) {
    ENSURE(cap.bar < 6 && cap.length != 0);
    uint32_t low = pci::read_bar(device.bus, device.slot, device.function, cap.bar);
    ENSURE((low & 1) == 0 && (low & 6) != 2 && (low & 6) != 6);
    uintptr_t pa = low & ~0xFULL;
    if ((low & 6) == 4) {
        ENSURE(cap.bar < 5);
        pa |= static_cast<uint64_t>(pci::read_bar(device.bus, device.slot, device.function, cap.bar + 1)) << 32;
    }
    ENSURE(pa != 0 && !__builtin_add_overflow(pa, cap.offset, &pa));
    return vmm::map_mmio(pa, cap.length, VM_WRITE | VM_NOCACHE);
}

Error PciTransport::initialize(const pci::DeviceInfo& device) {
    ENSURE(!common_, Error::Busy);
    auto common_cap = TRY(find_capability(device, CAP_COMMON));
    auto notify_cap = TRY(find_capability(device, CAP_NOTIFY));
    auto isr_cap = TRY(find_capability(device, CAP_ISR));
    ENSURE(common_cap.length >= 56 && (common_cap.offset & 3) == 0 && notify_cap.length >= 2 &&
           (notify_cap.offset & 1) == 0 && (notify_cap.multiplier & 1) == 0 &&
           (notify_cap.multiplier & (notify_cap.multiplier - 1)) == 0 && isr_cap.length >= 1);
    auto common = TRY(map_capability(device, common_cap));
    auto notify = TRY(map_capability(device, notify_cap));
    auto isr = TRY(map_capability(device, isr_cap));
    common_ = sys::move(common);
    notify_ = sys::move(notify);
    isr_ = sys::move(isr);
    notify_multiplier_ = notify_cap.multiplier;
    TRY(reset());
    set_status(STATUS_ACK);
    set_status(STATUS_ACK | STATUS_DRIVER);
    // Only VERSION_1 is negotiated, retaining the existing split-ring format.
    mmio::write32(common_->address(), 0x00, 1);
    arch_io_mb();
    ENSURE((mmio::read32(common_->address(), 0x04) & 1) != 0, Error::NotSupported);
    mmio::write32(common_->address(), 0x08, 0);
    arch_io_mb();
    mmio::write32(common_->address(), 0x0C, 0);
    arch_io_mb();
    mmio::write32(common_->address(), 0x08, 1);
    arch_io_mb();
    mmio::write32(common_->address(), 0x0C, 1);
    set_status(status() | STATUS_FEATURES_OK);
    ENSURE((status() & STATUS_FEATURES_OK) != 0, Error::NotSupported);
    return Error::None;
}

Result<uint16_t> PciTransport::queue_size(uint16_t index) {
    ENSURE(common_);
    mmio::write16(common_->address(), QUEUE_SELECT, index);
    arch_io_mb();
    uint16_t size = mmio::read16(common_->address(), QUEUE_SIZE);
    ENSURE(size != 0 && size <= 32768 && (size & (size - 1)) == 0, Error::NotSupported);
    return size;
}

Result<QueueNotification> PciTransport::configure_queue(uint16_t index, const SplitQueue& queue) {
    auto available_size = TRY(queue_size(index));
    ENSURE(queue.size() != 0 && queue.size() <= available_size && mmio::read16(common_->address(), QUEUE_ENABLE) == 0);
    size_t offset = static_cast<size_t>(mmio::read16(common_->address(), QUEUE_NOTIFY_OFFSET)) * notify_multiplier_;
    ENSURE(offset <= notify_->size() - sizeof(uint16_t));
    mmio::write16(common_->address(), QUEUE_SIZE, queue.size());
    write_address(0x20, queue.descriptor_pa());
    write_address(0x28, queue.available_pa());
    write_address(0x30, queue.used_pa());
    arch_io_mb();
    mmio::write16(common_->address(), QUEUE_ENABLE, 1);
    return QueueNotification(notify_->address() + offset, index);
}

void PciTransport::write_address(size_t offset, uintptr_t pa) {
    mmio::write32(common_->address(), offset, static_cast<uint32_t>(pa));
    mmio::write32(common_->address(), offset + 4, static_cast<uint32_t>(pa >> 32));
}
uint8_t PciTransport::status() const {
    return mmio::read8(common_->address(), STATUS_OFFSET);
}
void PciTransport::set_status(uint8_t value) {
    arch_io_mb();
    mmio::write8(common_->address(), STATUS_OFFSET, value);
    arch_io_mb();
}
void PciTransport::ready() {
    set_status(status() | STATUS_DRIVER_OK);
}
void QueueNotification::notify() const {
    if (address_ != 0) {
        // Publish descriptors and avail index before ringing the MMIO doorbell.
        arch_io_mb();
        mmio::write16(address_, index_);
    }
}
uint8_t PciTransport::acknowledge_irq() {
    uint8_t status = isr_ ? mmio::read8(isr_->address()) : 0;
    // Clear the interrupt before inspecting the device's used-ring writes.
    arch_io_mb();
    return status;
}

Error PciTransport::reset() {
    if (!common_) {
        return Error::None;
    }
    set_status(0);
    for (int remaining = 100000; remaining > 0; --remaining) {
        if (status() == 0) {
            // Observe reset completion before the owner can release DMA memory.
            arch_io_mb();
            return Error::None;
        }
        arch_spin_hint();
    }
    return Error::Timeout;
}

void PciTransport::release() {
    assert(!common_ || status() == 0);
    common_.reset();
    notify_.reset();
    isr_.reset();
    notify_multiplier_ = 0;
}

}  // namespace virtio
