#include "virtio_kbd.h"
#include <kernel/log.h>
#include "virtio/pci_transport.h"
#include "virtio/split_queue.h"
#include "cons/cons.h"
#include "cons/keyboard.h"
#include "drivers/intr.h"
#include "lib/stdio.h"
#include <asm/arch.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <sys/array.hpp>
#include <sys/iterator.hpp>

namespace {

constexpr uint16_t VIRTIO_VENDOR = 0x1AF4;
constexpr uint16_t VIRTIO_INPUT_DEV = 0x1052;
constexpr uint16_t EV_KEY = 1;

struct InputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
static_assert(sizeof(InputEvent) == 8);

// Input policy owns its event buffers and interprets completed events. PCI
// discovery/negotiation and ring layout live in the transport and queue layers.
class Keyboard {
public:
    Error start(const pci::DeviceInfo& device) {
        ENSURE(state_ == State::Uninitialized, Error::Busy);
        pci::CommandGuard command(device, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER);
        uint16_t enabled =
            static_cast<uint16_t>(pci::config_read32(device.bus, device.slot, device.function, pci::Command));
        pci::config_write32(device.bus, device.slot, device.function, pci::Command, enabled & ~pci::CMD_INTX_DISABLE);
        Error error = prepare(device);
        if (error != Error::None) {
            if (stop() != Error::None) {
                // DMA did not stop: retain mappings, queue and buffers together.
                command.commit();
            }
            return error;
        }
        intr::Guard guard;
        state_ = State::Ready;
        transport_.ready();
        command.commit();
        arch_irq_enable_line(irq_);
        notification_.notify();
        klog::info("input: VirtIO keyboard ready\n");
        klog::debug("virtio_kbd: ready, eventq=%d, IRQ=%d\n", queue_.size(), irq_);
        return Error::None;
    }

    void intr() {
        uint8_t status = transport_.acknowledge_irq();
        if (state_ != State::Ready || (status & 1) == 0) {
            return;
        }
        while (true) {
            auto completed = queue_.pop_used();
            if (!completed.ok()) {
                if (completed.error() != Error::NotFound) {
                    fail();
                }
                break;
            }
            const auto event = completed.value();
            if (event.length != sizeof(InputEvent)) {
                fail();
                break;
            }
            const InputEvent input = events_[event.id];
            if (input.type == EV_KEY && input.value <= 2) {
                int c = keyboard_.translate(input.code, input.value != 0);
                if (c >= 0) {
                    cons::push_input(static_cast<char>(c));
                }
            }
            if (queue_.submit(static_cast<uint16_t>(event.id)) != Error::None) {
                fail();
                break;
            }
        }
        if (state_ == State::Ready) {
            notification_.notify();
        }
    }

    int irq() const { return irq_; }

private:
    enum class State { Uninitialized, Ready, Quarantined };
    static constexpr uint16_t MAX_QUEUE_SIZE = 64;

    Error prepare(const pci::DeviceInfo& device) {
        TRY(transport_.initialize(device));
        uint16_t size = TRY(transport_.queue_size(0));
        if (size > MAX_QUEUE_SIZE) {
            size = MAX_QUEUE_SIZE;
        }
        TRY(queue_.allocate(size));
        for (uint16_t i = 0; i < size; ++i) {
            TRY(queue_.set_buffer(i, virt_to_phys(&events_[i]), sizeof(InputEvent), true));
            TRY(queue_.submit(i));
        }
        notification_ = TRY(transport_.configure_queue(0, queue_));
        uint32_t interrupt = pci::config_read32(device.bus, device.slot, device.function, pci::Interrupt);
        uint8_t pin = static_cast<uint8_t>(interrupt >> 8);
        ENSURE(pin >= 1 && pin <= 4, Error::NotSupported);
        irq_ = arch_pci_intx_to_irq(device.slot, pin);
        if (irq_ < 0) {
            // Platforms with firmware-assigned PCI lines use Interrupt Line.
            uint8_t line = static_cast<uint8_t>(interrupt);
            ENSURE(line != 0 && line != 0xFF, Error::NotSupported);
            irq_ = line;
        }
        return Error::None;
    }

    Error stop() {
        Error error = transport_.reset();
        if (error != Error::None) {
            state_ = State::Quarantined;
            return error;
        }
        queue_.release();
        notification_ = virtio::QueueNotification{};
        transport_.release();
        irq_ = -1;
        state_ = State::Uninitialized;
        keyboard_ = cons::KeyboardState{};
        return Error::None;
    }

    void fail() {
        klog::error("virtio_kbd: invalid event queue, stopping device\n");
        Error error = stop();
        if (error != Error::None) {
            klog::error("virtio_kbd: reset failed (%s), DMA resources retained\n", error_str(error));
        }
    }

    cons::KeyboardState keyboard_{};
    virtio::PciTransport transport_{};
    virtio::SplitQueue queue_{};
    virtio::QueueNotification notification_{};
    sys::array<InputEvent, MAX_QUEUE_SIZE> events_{};
    State state_{};
    int irq_ = -1;
};

class Manager {
public:
    static int init();
    static Error probe(const pci::DeviceInfo* device, const pci::DriverId*) {
        ENSURE(device);
        return keyboard_.start(*device);
    }
    static void intr() { keyboard_.intr(); }
    static int irq() { return keyboard_.irq(); }

private:
    inline static Keyboard keyboard_{};
    inline static bool registered_{};
};

const pci::DriverId VIRTIO_KBD_IDS[] = {
    {VIRTIO_VENDOR, VIRTIO_INPUT_DEV, pci::ANY_CLASS, pci::ANY_CLASS, pci::ANY_CLASS},
};
const pci::Driver VIRTIO_KBD_DRIVER = {
    "virtio_kbd",
    VIRTIO_KBD_IDS,
    static_cast<int>(sys::size(VIRTIO_KBD_IDS)),
    Manager::probe,
};

int Manager::init() {
    if (registered_) {
        return 0;
    }
    if (pci::register_driver(&VIRTIO_KBD_DRIVER) != Error::None) {
        return -1;
    }
    registered_ = true;
    return 0;
}

}  // namespace

namespace virtio_kbd {
int init() {
    return Manager::init();
}
void intr() {
    Manager::intr();
}
int irq() {
    return Manager::irq();
}
}  // namespace virtio_kbd
