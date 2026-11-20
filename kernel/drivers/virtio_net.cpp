#include "virtio_net.h"
#include "drivers/intr.h"
#include "drivers/pci.h"
#include "drivers/virtio/pci_transport.h"
#include "drivers/virtio/split_queue.h"
#include "fs/vfs_fs.h"
#include "fs/readiness.h"
#include "lib/waitqueue.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <abi/net.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <kernel/log.h>
#include <sys/array.hpp>

namespace {

constexpr uint16_t VIRTIO_NET_MODERN = 0x1041;
constexpr uint32_t FEATURE_MAC = 1U << 5;
constexpr uint16_t RX_QUEUE = 0;
constexpr uint16_t TX_QUEUE = 1;
constexpr uint16_t MAX_RX_BUFFERS = 16;
constexpr uint16_t MAX_TX_BUFFERS = 16;
constexpr size_t VIRTIO_HEADER_BYTES = 12;  // Modern VirtIO without offload metadata.
constexpr size_t DMA_BUFFER_BYTES = VIRTIO_HEADER_BYTES + NET_FRAME_MAX;
constexpr uint64_t IO_TIMEOUT_MS = 1000;
constexpr uint8_t ISR_QUEUE = 1;

class Device {
public:
    Error start(const pci::DeviceInfo& device) {
        ENSURE(state_ == State::Idle, Error::Busy);
        pci::CommandGuard command(device, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER | pci::CMD_INTX_DISABLE);
        Error error = prepare(device);
        if (error != Error::None) {
            if (stop() != Error::None)
                command.commit();
            return error;
        }
        {
            intr::Guard guard;
            state_ = State::Ready;
            transport_.ready();
            rx_notification_.notify();
        }
        command.commit();
        enable_interrupts(device);
        return Error::None;
    }

    Error acquire() {
        intr::Guard guard;
        ENSURE(state_ == State::Ready, Error::NoDevice);
        ENSURE(!opened_, Error::Busy);
        opened_ = true;
        return Error::None;
    }

    void release() {
        intr::Guard guard;
        opened_ = false;
    }

    Error mac(void* out) const {
        intr::Guard guard;
        ENSURE(state_ == State::Ready, Error::NoDevice);
        memcpy(out, mac_.data(), mac_.size());
        return Error::None;
    }

    Result<bool> read_ready() const {
        intr::Guard guard;
        ENSURE(state_ == State::Ready, Error::NoDevice);
        return rx_.has_used();
    }

    Result<int> receive(void* buf, size_t size) {
        ENSURE(size >= NET_FRAME_MAX, Error::Full);
        intr::Guard guard;
        uint64_t deadline = trap::uptime_ms() + IO_TIMEOUT_MS;
        do {
            TRY(check_io());
            auto completion = rx_.pop_used();
            if (!completion.ok()) {
                if (completion.error() != Error::NotFound)
                    return invalid_queue();
                progress_.sleep();
                continue;
            }
            const auto packet = completion.value();
            rx_announced_ = false;
            if (packet.length > DMA_BUFFER_BYTES)
                return invalid_queue();
            size_t length = 0;
            if (packet.length >= VIRTIO_HEADER_BYTES + NET_FRAME_MIN) {
                length = packet.length - VIRTIO_HEADER_BYTES;
                memcpy(buf, rx_buffers_[packet.id].data() + VIRTIO_HEADER_BYTES, length);
            }
            TRY(rx_.submit(static_cast<uint16_t>(packet.id)));
            rx_notification_.notify();
            if (length != 0)
                return static_cast<int>(length);
        } while (trap::uptime_ms() < deadline);
        return Error::Timeout;
    }

    Result<int> transmit(const void* buf, size_t size) {
        ENSURE(size >= NET_FRAME_MIN && size <= NET_FRAME_MAX, Error::Invalid);
        intr::Guard guard;
        uint64_t deadline = trap::uptime_ms() + IO_TIMEOUT_MS;
        TRY(check_io());
        TRY(reclaim_transmitted());
        // Transmit completions do not interrupt; a full ring waits for the next tick.
        while (tx_free_count_ == 0) {
            ENSURE(trap::uptime_ms() < deadline, Error::Timeout);
            progress_.sleep();
            TRY(check_io());
            TRY(reclaim_transmitted());
        }
        uint16_t id = tx_free_[--tx_free_count_];
        auto* data = tx_buffers_[id].data();
        memset(data, 0, VIRTIO_HEADER_BYTES);
        memcpy(data + VIRTIO_HEADER_BYTES, buf, size);
        TRY(tx_.set_buffer(id, virt_to_phys(data), static_cast<uint32_t>(VIRTIO_HEADER_BYTES + size), false));
        TRY(tx_.submit(id));
        tx_notification_.notify();
        return static_cast<int>(size);
    }

    // The timer tick is the fallback when the device has no usable interrupt.
    void tick() { signal(); }

    static void interrupt(void* context) {
        auto* device = static_cast<Device*>(context);
        uint8_t status = device->transport_.acknowledge_irq();
        if ((status & ISR_QUEUE) != 0 && device->state_ == State::Ready)
            device->signal();
    }

    Error stop() {
        pci::detach_irq(this);
        Error error = transport_.reset();
        if (error != Error::None) {
            state_ = State::Quarantined;
            klog::error("virtio_net: reset failed, DMA resources retained\n");
            return error;
        }
        rx_.release();
        tx_.release();
        for (auto& buffer : rx_buffers_)
            buffer = KernelBuffer{};
        for (auto& buffer : tx_buffers_)
            buffer = KernelBuffer{};
        rx_notification_ = {};
        tx_notification_ = {};
        tx_free_count_ = 0;
        rx_announced_ = false;
        transport_.release();
        state_ = State::Idle;
        return Error::None;
    }

private:
    enum class State { Idle, Ready, Quarantined };

    Error prepare(const pci::DeviceInfo& device) {
        TRY(transport_.initialize(device, FEATURE_MAC));
        TRY(transport_.read_device_config(device, mac_.data(), mac_.size()));
        auto receive_size = TRY(transport_.queue_size(RX_QUEUE));
        if (receive_size > MAX_RX_BUFFERS)
            receive_size = MAX_RX_BUFFERS;
        auto transmit_size = TRY(transport_.queue_size(TX_QUEUE));
        if (transmit_size > MAX_TX_BUFFERS)
            transmit_size = MAX_TX_BUFFERS;
        TRY(rx_.allocate(receive_size));
        TRY(tx_.allocate(transmit_size));
        tx_.suppress_interrupts();
        for (uint16_t i = 0; i < transmit_size; ++i) {
            tx_buffers_[i] = TRY(KernelBuffer::alloc(DMA_BUFFER_BYTES));
            tx_free_[tx_free_count_++] = i;
        }
        for (uint16_t i = 0; i < receive_size; ++i) {
            rx_buffers_[i] = TRY(KernelBuffer::alloc(DMA_BUFFER_BYTES));
            TRY(rx_.set_buffer(i, virt_to_phys(rx_buffers_[i].data()), DMA_BUFFER_BYTES, true));
            TRY(rx_.submit(i));
        }
        rx_notification_ = TRY(transport_.configure_queue(RX_QUEUE, rx_));
        tx_notification_ = TRY(transport_.configure_queue(TX_QUEUE, tx_));
        return Error::None;
    }

    // Receiving and the timer both wake blocked transmitters and announce frames.
    void signal() {
        if (!progress_.empty())
            progress_.wakeup_all();
        if (opened_ && state_ == State::Ready && rx_.has_used() && !rx_announced_) {
            rx_announced_ = true;
            fd::Readiness::notify();
        }
    }

    void enable_interrupts(const pci::DeviceInfo& device) {
        auto irq = pci::attach_irq(device, interrupt, this);
        if (!irq.ok()) {
            klog::warn("virtio_net: no interrupt line (%s), polling on the timer\n", error_str(irq.error()));
            return;
        }
        klog::debug("virtio_net: IRQ %d\n", irq.value());
    }

    Error reclaim_transmitted() {
        for (;;) {
            auto completion = tx_.pop_used();
            if (!completion.ok())
                return completion.error() == Error::NotFound ? Error::None : invalid_queue();
            uint32_t id = completion.value().id;
            if (id >= tx_.size() || tx_free_count_ >= tx_.size())
                return invalid_queue();
            tx_free_[tx_free_count_++] = static_cast<uint16_t>(id);
        }
    }

    Error check_io() const {
        ENSURE(state_ == State::Ready, Error::NoDevice);
        Task* task = sched::current();
        ENSURE(!task->termination_requested(), Error::Interrupted);
        return Error::None;
    }

    Error invalid_queue() {
        klog::error("virtio_net: invalid queue completion\n");
        static_cast<void>(stop());
        progress_.wakeup_all();
        fd::Readiness::notify();
        return Error::Io;
    }

    virtio::PciTransport transport_{};
    virtio::SplitQueue rx_{};
    virtio::SplitQueue tx_{};
    virtio::QueueNotification rx_notification_{};
    virtio::QueueNotification tx_notification_{};
    sys::array<KernelBuffer, MAX_RX_BUFFERS> rx_buffers_{};
    sys::array<KernelBuffer, MAX_TX_BUFFERS> tx_buffers_{};
    sys::array<uint16_t, MAX_TX_BUFFERS> tx_free_{};
    sys::array<uint8_t, NET_MAC_SIZE> mac_{};
    WaitQueue progress_{};
    State state_{};
    uint16_t tx_free_count_{};
    bool opened_{};
    bool rx_announced_{};
};

class NetFile : public vfs::File {
public:
    explicit NetFile(Device& device) : device_(device) {}
    ~NetFile() override { device_.release(); }
    bool is_stream() const override { return true; }
    bool is_duplex() const override { return true; }
    Result<bool> read_ready() override { return device_.read_ready(); }
    Result<int> read(void* buf, size_t size, size_t) override { return device_.receive(buf, size); }
    Result<int> write(const void* buf, size_t size, size_t) override { return device_.transmit(buf, size); }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::CharDevice, 0, 0);
        return Error::None;
    }
    Error control(unsigned int request, void* value) override {
        return request == NET_GET_MAC ? device_.mac(value) : Error::NotSupported;
    }

private:
    Device& device_;
};

class Manager {
public:
    static Error probe(const pci::DeviceInfo* device, const pci::DriverId*) {
        TRY(device_.start(*device));
        Error error = vfs::register_char_dev("net0", open);
        if (error != Error::None) {
            static_cast<void>(device_.stop());
            return error;
        }
        klog::info("net: VirtIO Ethernet ready at /dev/net0\n");
        return Error::None;
    }

    static void tick() { device_.tick(); }

private:
    static Result<vfs::FileHandle> open() {
        TRY(device_.acquire());
        vfs::FileHandle file(new (sys::nothrow) NetFile(device_));
        if (!file) {
            device_.release();
            return Error::NoMem;
        }
        return file;
    }

    inline static Device device_{};
};

const pci::DriverId NET_IDS[] = {
    {virtio::PCI_VENDOR, VIRTIO_NET_MODERN, pci::ANY_CLASS, pci::ANY_CLASS, pci::ANY_CLASS}};
const pci::Driver NET_DRIVER = {"virtio_net", NET_IDS, 1, Manager::probe};

}  // namespace

int virtio_net::init() {
    return static_cast<int>(pci::register_driver(&NET_DRIVER));
}

void virtio_net::tick() {
    Manager::tick();
}
