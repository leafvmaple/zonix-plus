#include "virtio_input.h"
#include <kernel/log.h>
#include "virtio/pci_transport.h"
#include "virtio/split_queue.h"
#include "cons/cons.h"
#include <abi/display.h>
#include "drivers/intr.h"
#include "drivers/pci.h"
#include "lib/stdio.h"
#include <asm/arch.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <sys/array.hpp>
#include <sys/iterator.hpp>

namespace {

constexpr uint16_t VIRTIO_INPUT_DEV = 0x1052;
constexpr uint16_t EV_KEY = 1;
constexpr uint16_t EV_REL = 2;

struct InputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
static_assert(sizeof(InputEvent) == 8);

// Input policy owns its event buffers and interprets completed events. PCI
// discovery/negotiation and ring layout live in the transport and queue layers.
class Device {
public:
    bool available() const { return state_ == State::Uninitialized; }

    Error start(const pci::DeviceInfo& device) {
        ENSURE(state_ == State::Uninitialized, Error::Busy);
        pci::CommandGuard command(device, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER);
        Error error = prepare(device);
        if (error == Error::None) {
            auto irq = pci::attach_irq(device, interrupt, this);
            error = irq.ok() ? Error::None : irq.error();
            if (irq.ok())
                irq_ = irq.value();
        }
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
        notification_.notify();
        klog::info("input: VirtIO device ready (eventq=%d, IRQ=%d)\n", queue_.size(), irq_);
        return Error::None;
    }

private:
    enum class State { Uninitialized, Ready, Quarantined };
    static constexpr uint16_t MAX_QUEUE_SIZE = 64;

    static void interrupt(void* context) { static_cast<Device*>(context)->intr(); }

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
            dispatch(input);
            if (queue_.submit(static_cast<uint16_t>(event.id)) != Error::None) {
                fail();
                break;
            }
        }
        if (state_ == State::Ready) {
            notification_.notify();
        }
    }

    static void dispatch(const InputEvent& input) {
        if (input.type == EV_KEY && input.value <= DISPLAY_KEY_REPEAT) {
            if (input.code >= DISPLAY_BUTTON_LEFT && input.code <= DISPLAY_BUTTON_MIDDLE) {
                if (input.value != DISPLAY_KEY_REPEAT)
                    cons::pointer_event(DISPLAY_EVENT_BUTTON, input.code, static_cast<int>(input.value));
            } else if (input.code < 256) {
                cons::key_event(input.code, static_cast<int>(input.value));
            }
        } else if (input.type == EV_REL &&
                   (input.code == DISPLAY_REL_X || input.code == DISPLAY_REL_Y || input.code == DISPLAY_REL_WHEEL)) {
            cons::pointer_event(DISPLAY_EVENT_REL, input.code, static_cast<int32_t>(input.value));
        }
    }

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
        return Error::None;
    }

    Error stop() {
        pci::detach_irq(this);
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
        return Error::None;
    }

    void fail() {
        klog::error("virtio_input: invalid event queue, stopping device\n");
        Error error = stop();
        if (error != Error::None) {
            klog::error("virtio_input: reset failed (%s), DMA resources retained\n", error_str(error));
        }
    }

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
        for (auto& input : devices_) {
            if (input.available())
                return input.start(*device);
        }
        return Error::Full;
    }

private:
    inline static sys::array<Device, 2> devices_{};
    inline static bool registered_{};
};

const pci::DriverId VIRTIO_INPUT_IDS[] = {
    {virtio::PCI_VENDOR, VIRTIO_INPUT_DEV, pci::ANY_CLASS, pci::ANY_CLASS, pci::ANY_CLASS},
};
const pci::Driver VIRTIO_INPUT_DRIVER = {
    "virtio_input",
    VIRTIO_INPUT_IDS,
    static_cast<int>(sys::size(VIRTIO_INPUT_IDS)),
    Manager::probe,
};

int Manager::init() {
    if (registered_) {
        return 0;
    }
    if (pci::register_driver(&VIRTIO_INPUT_DRIVER) != Error::None) {
        return -1;
    }
    registered_ = true;
    return 0;
}

}  // namespace

namespace virtio_input {
int init() {
    return Manager::init();
}
}  // namespace virtio_input
