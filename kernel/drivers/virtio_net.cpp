#include "virtio_net.h"
#include "drivers/intr.h"
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

constexpr uint16_t VIRTIO_VENDOR = 0x1AF4;
constexpr uint16_t VIRTIO_NET_MODERN = 0x1041;
constexpr uint32_t FEATURE_MAC = 1U << 5;
constexpr uint16_t RX_QUEUE = 0;
constexpr uint16_t TX_QUEUE = 1;
constexpr uint16_t MAX_RX_BUFFERS = 8;
constexpr size_t VIRTIO_HEADER_BYTES = 12;  // Modern VirtIO without offload metadata.
constexpr size_t DMA_BUFFER_BYTES = VIRTIO_HEADER_BYTES + NET_FRAME_MAX;
constexpr uint64_t IO_TIMEOUT_MS = 1000;

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
        state_ = State::Ready;
        transport_.ready();
        rx_notification_.notify();
        command.commit();
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
        while (tx_busy_) {
            auto completion = tx_.pop_used();
            if (completion.ok()) {
                tx_busy_ = false;
                break;
            }
            if (completion.error() != Error::NotFound)
                return invalid_queue();
            ENSURE(trap::uptime_ms() < deadline, Error::Timeout);
            progress_.sleep();
            TRY(check_io());
        }
        auto* data = tx_buffer_.data();
        memset(data, 0, VIRTIO_HEADER_BYTES);
        memcpy(data + VIRTIO_HEADER_BYTES, buf, size);
        TRY(tx_.set_buffer(0, virt_to_phys(data), static_cast<uint32_t>(VIRTIO_HEADER_BYTES + size), false));
        TRY(tx_.submit(0));
        tx_busy_ = true;
        tx_notification_.notify();
        return static_cast<int>(size);
    }

    void tick() {
        if (!progress_.empty())
            progress_.wakeup_all();
        if (opened_ && state_ == State::Ready && rx_.has_used() && !rx_announced_) {
            rx_announced_ = true;
            fd::Readiness::notify();
        }
    }

    Error stop() {
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
        tx_buffer_ = KernelBuffer{};
        rx_notification_ = {};
        tx_notification_ = {};
        tx_busy_ = false;
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
        TRY(rx_.allocate(receive_size));
        TRY(tx_.allocate(1));
        rx_.suppress_interrupts();
        tx_.suppress_interrupts();
        tx_buffer_ = TRY(KernelBuffer::alloc(DMA_BUFFER_BYTES));
        for (uint16_t i = 0; i < receive_size; ++i) {
            rx_buffers_[i] = TRY(KernelBuffer::alloc(DMA_BUFFER_BYTES));
            TRY(rx_.set_buffer(i, virt_to_phys(rx_buffers_[i].data()), DMA_BUFFER_BYTES, true));
            TRY(rx_.submit(i));
        }
        rx_notification_ = TRY(transport_.configure_queue(RX_QUEUE, rx_));
        tx_notification_ = TRY(transport_.configure_queue(TX_QUEUE, tx_));
        return Error::None;
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
    KernelBuffer tx_buffer_{};
    sys::array<uint8_t, NET_MAC_SIZE> mac_{};
    WaitQueue progress_{};
    State state_{};
    bool tx_busy_{};
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

const pci::DriverId NET_IDS[] = {{VIRTIO_VENDOR, VIRTIO_NET_MODERN, pci::ANY_CLASS, pci::ANY_CLASS, pci::ANY_CLASS}};
const pci::Driver NET_DRIVER = {"virtio_net", NET_IDS, 1, Manager::probe};

}  // namespace

int virtio_net::init() {
    return static_cast<int>(pci::register_driver(&NET_DRIVER));
}

void virtio_net::tick() {
    Manager::tick();
}
