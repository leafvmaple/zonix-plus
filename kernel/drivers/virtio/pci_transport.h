#pragma once

#include "drivers/pci.h"
#include "mm/mmio.h"

namespace virtio {

class SplitQueue;

// Borrowed notification binding for one queue. Valid until transport reset.
class QueueNotification {
public:
    QueueNotification() = default;
    void notify() const;

private:
    friend class PciTransport;
    QueueNotification(uintptr_t address, uint16_t index) : address_(address), index_(index) {}
    uintptr_t address_{};
    uint16_t index_{};
};

// Modern PCI transport only. DMA queues remain owned by the device driver.
// Reset must succeed before releasing a queue or its mapped transport.
class PciTransport {
public:
    PciTransport() = default;
    PciTransport(const PciTransport&) = delete;
    PciTransport& operator=(const PciTransport&) = delete;
    Error initialize(const pci::DeviceInfo& device);
    Result<uint16_t> queue_size(uint16_t index);
    Result<QueueNotification> configure_queue(uint16_t index, const SplitQueue& queue);
    void ready();
    uint8_t acknowledge_irq();
    Error reset();
    void release();

private:
    struct Capability {
        uint8_t bar;
        uint32_t offset;
        uint32_t length;
        uint32_t multiplier;
    };
    static Result<Capability> find_capability(const pci::DeviceInfo& device, uint8_t type);
    static Result<vmm::MmioRegion> map_capability(const pci::DeviceInfo& device, const Capability& cap);
    uint8_t status() const;
    void set_status(uint8_t value);
    void write_address(size_t offset, uintptr_t pa);
    vmm::MmioRegion common_{};
    vmm::MmioRegion notify_{};
    vmm::MmioRegion isr_{};
    uint32_t notify_multiplier_{};
};

}  // namespace virtio
