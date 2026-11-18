#include "virtio_rng.h"
#include "drivers/intr.h"
#include "drivers/virtio/pci_transport.h"
#include "drivers/virtio/split_queue.h"
#include "lib/mutex.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <asm/mmu.h>
#include <kernel/log.h>

namespace {
constexpr uint16_t VIRTIO_VENDOR = 0x1AF4;
constexpr uint16_t VIRTIO_RNG_MODERN = 0x1044;
constexpr uint16_t REQUEST_QUEUE = 0;
constexpr uint16_t REQUEST_DESCRIPTOR = 0;
constexpr uint64_t REQUEST_TIMEOUT_MS = 1000;

struct UnlockMutex {
    void operator()(Mutex* mutex) const noexcept { mutex->unlock(); }
};

class Device {
public:
    Error start(const pci::DeviceInfo& device) {
        ENSURE(state_ == State::Offline, Error::Busy);
        pci::CommandGuard command(device, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER | pci::CMD_INTX_DISABLE);
        Error error = prepare(device);
        if (error != Error::None) {
            if (stop() != Error::None)
                command.commit();
            return error;
        }
        transport_.ready();
        state_ = State::Ready;
        klog::info("random: VirtIO entropy source ready\n");
        command.commit();
        return Error::None;
    }

    Result<size_t> read(void* output, size_t size) {
        ENSURE(size != 0 && size <= virtio_rng::MAX_READ);
        TRY(mutex_.lock_interruptible());
        sys::unique_ptr<Mutex, UnlockMutex> lock(&mutex_);
        intr::Guard guard;
        ENSURE(state_ == State::Ready, Error::NoDevice);
        // An interrupted or timed-out caller leaves the DMA request in flight.
        // A later caller can consume its still-unused bytes after completion.
        if (pending_ == 0) {
            TRY(queue_.set_buffer(REQUEST_DESCRIPTOR, virt_to_phys(buffer_.data()), static_cast<uint32_t>(size), true));
            TRY(queue_.submit(REQUEST_DESCRIPTOR));
            pending_ = size;
            notification_.notify();
        }
        const auto deadline = trap::uptime_ms() + REQUEST_TIMEOUT_MS;
        while (true) {
            ENSURE(!sched::current()->termination_requested(), Error::Interrupted);
            auto completion = queue_.pop_used();
            if (completion.ok()) {
                size_t length = completion.value().length;
                if (length == 0 || length > pending_)
                    return invalid_completion();
                pending_ = 0;
                size_t count = length < size ? length : size;
                memcpy(output, buffer_.data(), count);
                memset(buffer_.data(), 0, length);
                return count;
            }
            if (completion.error() != Error::NotFound)
                return invalid_completion();
            ENSURE(trap::uptime_ms() < deadline, Error::Timeout);
            progress_.sleep();
        }
    }

    void tick() {
        if (!progress_.empty())
            progress_.wakeup_all();
    }

private:
    enum class State { Offline, Ready, Quarantined };

    Error prepare(const pci::DeviceInfo& device) {
        TRY(transport_.initialize(device));
        TRY(queue_.allocate(1));
        queue_.suppress_interrupts();
        buffer_ = TRY(KernelBuffer::alloc(virtio_rng::MAX_READ));
        memset(buffer_.data(), 0, buffer_.size());
        notification_ = TRY(transport_.configure_queue(REQUEST_QUEUE, queue_));
        return Error::None;
    }

    Error stop() {
        Error error = transport_.reset();
        if (error != Error::None) {
            state_ = State::Quarantined;
            klog::error("virtio_rng: reset failed, DMA resources retained\n");
            return error;
        }
        if (buffer_.data())
            memset(buffer_.data(), 0, buffer_.size());
        buffer_ = KernelBuffer{};
        queue_.release();
        notification_ = {};
        pending_ = 0;
        transport_.release();
        state_ = State::Offline;
        return Error::None;
    }

    Error invalid_completion() {
        klog::error("virtio_rng: invalid queue completion\n");
        static_cast<void>(stop());
        return Error::Io;
    }

    virtio::PciTransport transport_{};
    virtio::SplitQueue queue_{};
    virtio::QueueNotification notification_{};
    KernelBuffer buffer_{};
    Mutex mutex_{};
    WaitQueue progress_{};
    State state_{};
    size_t pending_{};
};

class Manager {
public:
    static Error probe(const pci::DeviceInfo* device, const pci::DriverId*) { return device_.start(*device); }
    static Result<size_t> read(void* output, size_t size) { return device_.read(output, size); }
    static void tick() { device_.tick(); }

private:
    inline static Device device_{};
};

const pci::DriverId RNG_IDS[] = {{VIRTIO_VENDOR, VIRTIO_RNG_MODERN, pci::ANY_CLASS, pci::ANY_CLASS, pci::ANY_CLASS}};
const pci::Driver RNG_DRIVER = {"virtio_rng", RNG_IDS, 1, Manager::probe};
}  // namespace

int virtio_rng::init() {
    return static_cast<int>(pci::register_driver(&RNG_DRIVER));
}

void virtio_rng::tick() {
    Manager::tick();
}

Result<size_t> virtio_rng::read(void* output, size_t size) {
    return Manager::read(output, size);
}
