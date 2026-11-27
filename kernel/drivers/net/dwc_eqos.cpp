#include "drivers/net/dwc_eqos.h"
#include "drivers/intr.h"
#include "drivers/mmio.h"
#include "drivers/pci.h"
#include "fs/readiness.h"
#include "fs/vfs_fs.h"
#include "lib/string.h"
#include "lib/waitqueue.h"
#include "mm/dma.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <abi/net.h>
#include <asm/arch.h>
#include <kernel/log.h>
#include <sys/array.hpp>

namespace {

namespace reg {
constexpr uintptr_t MAC_CONFIGURATION = 0x0000;
constexpr uintptr_t MAC_PACKET_FILTER = 0x0008;
constexpr uintptr_t MAC_RXQ_CTRL0 = 0x00A0;
constexpr uintptr_t MAC_INTERRUPT_ENABLE = 0x00B4;
constexpr uintptr_t MAC_VERSION = 0x0110;
constexpr uintptr_t MAC_MDIO_ADDRESS = 0x0200;
constexpr uintptr_t MAC_MDIO_DATA = 0x0204;
constexpr uintptr_t MAC_ADDRESS0_HIGH = 0x0300;
constexpr uintptr_t MAC_ADDRESS0_LOW = 0x0304;
constexpr uintptr_t MMC_RX_INTERRUPT_MASK = 0x070C;
constexpr uintptr_t MMC_TX_INTERRUPT_MASK = 0x0710;
constexpr uintptr_t MMC_IPC_RX_INTERRUPT_MASK = 0x0800;
constexpr uintptr_t MTL_TXQ0_OPERATION_MODE = 0x0D00;
constexpr uintptr_t MTL_RXQ0_OPERATION_MODE = 0x0D30;
constexpr uintptr_t DMA_MODE = 0x1000;
constexpr uintptr_t DMA_SYSBUS_MODE = 0x1004;
constexpr uintptr_t DMA_CH0_TX_CONTROL = 0x1104;
constexpr uintptr_t DMA_CH0_RX_CONTROL = 0x1108;
constexpr uintptr_t DMA_CH0_TX_BASE_HIGH = 0x1110;
constexpr uintptr_t DMA_CH0_TX_BASE_LOW = 0x1114;
constexpr uintptr_t DMA_CH0_RX_BASE_HIGH = 0x1118;
constexpr uintptr_t DMA_CH0_RX_BASE_LOW = 0x111C;
constexpr uintptr_t DMA_CH0_TX_TAIL = 0x1120;
constexpr uintptr_t DMA_CH0_RX_TAIL = 0x1128;
constexpr uintptr_t DMA_CH0_TX_RING_LENGTH = 0x112C;
constexpr uintptr_t DMA_CH0_RX_RING_LENGTH = 0x1130;
constexpr uintptr_t DMA_CH0_INTERRUPT_ENABLE = 0x1134;
constexpr uintptr_t DMA_CH0_STATUS = 0x1160;
}  // namespace reg

// MAC configuration: receiver and transmitter enables, full duplex, the
// port-select and speed bits that together give 10/100/1000, and stripping
// of the frame check sequence from received frames.
constexpr uint32_t MAC_RE = 1U << 0;
constexpr uint32_t MAC_TE = 1U << 1;
constexpr uint32_t MAC_DM = 1U << 13;
constexpr uint32_t MAC_FES = 1U << 14;
constexpr uint32_t MAC_PS = 1U << 15;
constexpr uint32_t MAC_ACS = 1U << 20;
constexpr uint32_t MAC_CST = 1U << 21;
constexpr uint32_t MAC_SPEED_MASK = MAC_PS | MAC_FES | MAC_DM;
constexpr uint32_t FILTER_PASS_ALL_MULTICAST = 1U << 4;
constexpr uint32_t RXQ0_ENABLE_DCB = 2;
constexpr uint32_t VERSION_SNPS_MASK = 0xFF;
constexpr uint32_t ADDRESS_ENABLE = 1U << 31;

// MDIO: busy/start bit, read operation, clock range (5 divides
// a 250-300 MHz CSR clock by 124, staying under 2.5 MHz below that too),
// register and PHY address.
constexpr uint32_t MDIO_BUSY = 1U << 0;
constexpr uint32_t MDIO_READ = 3U << 2;
constexpr uint32_t MDIO_CLOCK_RANGE = 5U << 8;
constexpr uint32_t MDIO_REGISTER_SHIFT = 16;
constexpr uint32_t MDIO_PHY_SHIFT = 21;
constexpr uint32_t MDIO_DATA_MASK = 0xFFFF;

// MTL queue 0: store-and-forward both ways, transmit queue enabled, and the
// queue sizes in 256-byte units minus one for the 2 KiB FIFOs.
constexpr uint32_t TXQ_STORE_FORWARD = 1U << 1;
constexpr uint32_t TXQ_ENABLE = 2U << 2;
constexpr uint32_t TXQ_SIZE_SHIFT = 16;
constexpr uint32_t RXQ_FORWARD_UNDERSIZED = 1U << 3;
constexpr uint32_t RXQ_STORE_FORWARD = 1U << 5;
constexpr uint32_t RXQ_SIZE_SHIFT = 20;
constexpr uint32_t FIFO_BYTES = 2048;
constexpr uint32_t QUEUE_SIZE = FIFO_BYTES / 256 - 1;

// DMA: software reset, bus bursts, outstanding requests, channel starts,
// burst lengths and the receive buffer size field (bits 14:1, in bytes).
constexpr uint32_t DMA_SOFTWARE_RESET = 1U << 0;
constexpr uint32_t SYSBUS_FIXED_BURST = 1U << 0;
constexpr uint32_t SYSBUS_BLEN4 = 1U << 1;
constexpr uint32_t SYSBUS_BLEN8 = 1U << 2;
constexpr uint32_t SYSBUS_BLEN16 = 1U << 3;
constexpr uint32_t SYSBUS_READ_LIMIT_SHIFT = 16;
constexpr uint32_t SYSBUS_WRITE_LIMIT_SHIFT = 24;
constexpr uint32_t SYSBUS_OUTSTANDING = 15;
constexpr uint32_t CHANNEL_START = 1U << 0;
constexpr uint32_t TX_OPERATE_ON_SECOND = 1U << 4;
constexpr uint32_t BURST_SHIFT = 16;
constexpr uint32_t BURST_LENGTH = 16;
constexpr uint32_t RX_BUFFER_SIZE_SHIFT = 1;

// Normal descriptors: four little-endian words. The last word carries the
// ownership bit and, after the controller wrote back, the frame status.
constexpr uint32_t DESC_OWN = 1U << 31;
constexpr uint32_t RDES3_INTERRUPT = 1U << 30;  // As handed to the controller
constexpr uint32_t RDES3_CONTEXT = 1U << 30;    // As written back
constexpr uint32_t TDES2_INTERRUPT = 1U << 31;
constexpr uint32_t DESC_FIRST = 1U << 29;
constexpr uint32_t DESC_LAST = 1U << 28;
constexpr uint32_t RDES3_BUFFER1_VALID = 1U << 24;
constexpr uint32_t RDES3_ERROR = 1U << 15;
constexpr uint32_t RDES3_LENGTH_MASK = 0x7FFF;
constexpr uint32_t FCS_BYTES = 4;

// DMA channel interrupts, enabled and reported at the same bit positions:
// frame transmitted, frame received, no receive buffer left, fatal bus
// error, and the abnormal and normal summaries. Status bits are cleared by
// writing them back. Statistics counters can interrupt too and are masked.
constexpr uint32_t DMA_INT_TRANSMIT = 1U << 0;
constexpr uint32_t DMA_INT_RECEIVE = 1U << 6;
constexpr uint32_t DMA_INT_RECEIVE_UNAVAILABLE = 1U << 7;
constexpr uint32_t DMA_INT_FATAL_BUS_ERROR = 1U << 12;
constexpr uint32_t DMA_INT_ABNORMAL = 1U << 14;
constexpr uint32_t DMA_INT_NORMAL = 1U << 15;
constexpr uint32_t DMA_INTERRUPTS = DMA_INT_TRANSMIT | DMA_INT_RECEIVE | DMA_INT_RECEIVE_UNAVAILABLE |
                                    DMA_INT_FATAL_BUS_ERROR | DMA_INT_ABNORMAL | DMA_INT_NORMAL;
constexpr uint32_t MMC_MASK_ALL = 0xFFFFFFFF;
// An interrupt line that keeps firing with nothing for the channel to report
// is one this driver cannot clear; after this many in a row it goes back to
// polling on the timer tick.
constexpr uint32_t EMPTY_INTERRUPT_LIMIT = 1000;

// Standard PHY registers and the bits that tell the negotiated link.
namespace phy {
constexpr uint32_t STATUS = 1;
constexpr uint32_t ID1 = 2;
constexpr uint32_t ADVERTISE = 4;
constexpr uint32_t PARTNER = 5;
constexpr uint32_t GIGABIT_CONTROL = 9;
constexpr uint32_t GIGABIT_STATUS = 10;
constexpr uint32_t STATUS_LINK = 1U << 2;
constexpr uint32_t ABILITY_100_FULL = 1U << 8;
constexpr uint32_t ABILITY_100_HALF = 1U << 7;
constexpr uint32_t ABILITY_10_FULL = 1U << 6;
constexpr uint32_t ADVERTISE_1000_FULL = 1U << 9;
constexpr uint32_t ADVERTISE_1000_HALF = 1U << 8;
constexpr uint32_t PARTNER_1000_FULL = 1U << 11;
constexpr uint32_t PARTNER_1000_HALF = 1U << 10;
constexpr uint32_t ADDRESSES = 32;
constexpr uint32_t ABSENT = 0xFFFF;
}  // namespace phy

struct Descriptor {
    uint32_t word[4];
};
static_assert(sizeof(Descriptor) == 16);

// Descriptors sharing a cache line with one the controller owns must not be
// written back, so receive descriptors are handed back a line at a time and
// one frame is transmitted at a time.
constexpr size_t CACHE_LINE = 64;
constexpr size_t PER_LINE = CACHE_LINE / sizeof(Descriptor);
constexpr size_t RX_COUNT = 32;
constexpr size_t TX_COUNT = PER_LINE;
constexpr size_t BUFFER_BYTES = 2048;
static_assert(RX_COUNT % PER_LINE == 0 && NET_FRAME_MAX + FCS_BYTES <= BUFFER_BYTES);

static_assert(dwc_eqos::MAC_BYTES == NET_MAC_SIZE);

constexpr int POLL_LIMIT = 1000000;
constexpr uint64_t IO_TIMEOUT_MS = 1000;
constexpr uint64_t LINK_POLL_MS = 1000;

class Device {
public:
    Error start(uintptr_t base, const sys::array<uint8_t, dwc_eqos::MAC_BYTES>* board_mac, int irq) {
        ENSURE(base_ == 0, Error::Busy);
        base_ = base;
        const uint32_t version = mmio::read32(base_, reg::MAC_VERSION);
        // Firmware that used the controller may have left its address in it;
        // the software reset clears it.
        choose_address(board_mac);
        mmio::write32(base_, reg::DMA_MODE, DMA_SOFTWARE_RESET);
        int polls = 0;
        while ((mmio::read32(base_, reg::DMA_MODE) & DMA_SOFTWARE_RESET) != 0 && polls < POLL_LIMIT) {
            arch_spin_hint();
            ++polls;
        }
        if (polls == POLL_LIMIT) {
            base_ = 0;
            klog::error("dwc_eqos: software reset did not finish (no PHY clock?)\n");
            return Error::Timeout;
        }
        Error error = allocate();
        if (error != Error::None) {
            release();
            return error;
        }
        program();
        phy_ = find_phy();
        if (phy_ == phy::ADDRESSES)
            klog::warn("dwc_eqos: no PHY answers on MDIO\n");
        update_link();
        state_ = State::Ready;
        enable_interrupts(irq);
        klog::info("dwc_eqos: version 0x%x, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", version & VERSION_SNPS_MASK, mac_[0],
                   mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);
        return Error::None;
    }

    bool started() const { return state_ == State::Ready; }

    Error acquire() {
        intr::Guard guard;
        ENSURE(state_ == State::Ready, Error::NoDevice);
        ENSURE(!opened_, Error::Busy);
        opened_ = true;
        return Error::None;
    }

    void release_file() {
        intr::Guard guard;
        opened_ = false;
    }

    Error mac(void* out) const {
        ENSURE(state_ == State::Ready, Error::NoDevice);
        memcpy(out, mac_.data(), mac_.size());
        return Error::None;
    }

    Result<bool> read_ready() {
        intr::Guard guard;
        ENSURE(state_ == State::Ready, Error::NoDevice);
        return received();
    }

    Result<int> receive(void* out, size_t size) {
        ENSURE(size >= NET_FRAME_MAX, Error::Full);
        intr::Guard guard;
        const uint64_t deadline = trap::uptime_ms() + IO_TIMEOUT_MS;
        do {
            TRY(check_io());
            if (!received()) {
                progress_.sleep();
                continue;
            }
            const size_t index = rx_next_;
            const uint32_t status = rx_descriptor(index).word[3];
            size_t length = status & RDES3_LENGTH_MASK;
            // A frame whose check sequence was kept is four bytes longer.
            if (length > NET_FRAME_MAX && length - FCS_BYTES <= NET_FRAME_MAX)
                length -= FCS_BYTES;
            const bool whole = (status & (DESC_FIRST | DESC_LAST)) == (DESC_FIRST | DESC_LAST) &&
                               (status & (RDES3_ERROR | RDES3_CONTEXT)) == 0 && length >= NET_FRAME_MIN &&
                               length <= NET_FRAME_MAX;
            if (whole) {
                rx_buffers_.sync_for_cpu(index * BUFFER_BYTES, length);
                memcpy(out, static_cast<const uint8_t*>(rx_buffers_.data()) + index * BUFFER_BYTES, length);
            }
            rx_announced_ = false;
            consume(index);
            if (whole)
                return static_cast<int>(length);
        } while (trap::uptime_ms() < deadline);
        return Error::Timeout;
    }

    Result<int> transmit(const void* frame, size_t size) {
        ENSURE(size >= NET_FRAME_MIN && size <= NET_FRAME_MAX, Error::Invalid);
        intr::Guard guard;
        const uint64_t deadline = trap::uptime_ms() + IO_TIMEOUT_MS;
        TRY(check_io());
        // The previous frame shares the cache line, so it must be sent first.
        while (transmitting()) {
            ENSURE(trap::uptime_ms() < deadline, Error::Timeout);
            progress_.sleep();
            TRY(check_io());
        }
        const size_t index = tx_next_;
        memcpy(static_cast<uint8_t*>(tx_buffers_.data()) + index * BUFFER_BYTES, frame, size);
        tx_buffers_.sync_for_device(index * BUFFER_BYTES, size);
        const uintptr_t buffer = tx_buffers_.pa() + index * BUFFER_BYTES;
        Descriptor& descriptor = tx_descriptor(index);
        descriptor.word[0] = static_cast<uint32_t>(buffer);
        descriptor.word[1] = static_cast<uint32_t>(buffer >> 32);
        descriptor.word[2] = TDES2_INTERRUPT | static_cast<uint32_t>(size);
        descriptor.word[3] = DESC_OWN | DESC_FIRST | DESC_LAST | static_cast<uint32_t>(size);
        tx_ring_.sync_for_device(0, TX_COUNT * sizeof(Descriptor));
        tx_next_ = (index + 1) % TX_COUNT;
        mmio::write32(base_, reg::DMA_CH0_TX_TAIL,
                      static_cast<uint32_t>(tx_ring_.pa() + tx_next_ * sizeof(Descriptor)));
        return static_cast<int>(size);
    }

    // The timer tick polls when the controller has no usable interrupt and
    // catches anything an interrupt missed.
    void tick() {
        if (state_ != State::Ready)
            return;
        if (interrupts_stopped_ && !storm_reported_) {
            storm_reported_ = true;
            klog::warn("dwc_eqos: interrupt line keeps firing, polling on the timer instead\n");
        }
        // A stalled channel records why in its status.
        mmio::write32(base_, reg::DMA_CH0_STATUS, mmio::read32(base_, reg::DMA_CH0_STATUS));
        wake();
        const uint64_t now = trap::uptime_ms();
        if (now >= link_poll_ms_) {
            link_poll_ms_ = now + LINK_POLL_MS;
            update_link();
        }
    }

private:
    enum class State { Idle, Ready };

    void wake() {
        if (!progress_.empty())
            progress_.wakeup_all();
        if (opened_ && !rx_announced_ && received()) {
            rx_announced_ = true;
            fd::Readiness::notify();
        }
    }

    void enable_interrupts(int irq) {
        mmio::write32(base_, reg::MMC_RX_INTERRUPT_MASK, MMC_MASK_ALL);
        mmio::write32(base_, reg::MMC_TX_INTERRUPT_MASK, MMC_MASK_ALL);
        mmio::write32(base_, reg::MMC_IPC_RX_INTERRUPT_MASK, MMC_MASK_ALL);
        mmio::write32(base_, reg::MAC_INTERRUPT_ENABLE, 0);
        if (irq <= 0)
            return;
        Error error = pci::attach_line(irq, interrupt, this);
        if (error != Error::None) {
            klog::warn("dwc_eqos: IRQ %d not attached (%s), polling on the timer\n", irq, error_str(error));
            return;
        }
        mmio::write32(base_, reg::DMA_CH0_INTERRUPT_ENABLE, DMA_INTERRUPTS);
        klog::debug("dwc_eqos: IRQ %d\n", irq);
    }

    static void interrupt(void* context) {
        auto* device = static_cast<Device*>(context);
        const uint32_t status = mmio::read32(device->base_, reg::DMA_CH0_STATUS) & DMA_INTERRUPTS;
        if (status == 0) {
            if (++device->empty_interrupts_ >= EMPTY_INTERRUPT_LIMIT && !device->interrupts_stopped_) {
                device->interrupts_stopped_ = true;
                mmio::write32(device->base_, reg::DMA_CH0_INTERRUPT_ENABLE, 0);
            }
            return;
        }
        device->empty_interrupts_ = 0;
        mmio::write32(device->base_, reg::DMA_CH0_STATUS, status);
        device->wake();
    }

    Descriptor& rx_descriptor(size_t index) { return static_cast<Descriptor*>(rx_ring_.data())[index]; }
    Descriptor& tx_descriptor(size_t index) { return static_cast<Descriptor*>(tx_ring_.data())[index]; }

    static bool usable(const sys::array<uint8_t, dwc_eqos::MAC_BYTES>& mac) {
        bool zero = true;
        bool ones = true;
        for (uint8_t byte : mac) {
            zero = zero && byte == 0;
            ones = ones && byte == 0xFF;
        }
        return !zero && !ones && (mac[0] & 1) == 0;
    }

    // The board's address from firmware, else the one left in the
    // controller, else a fixed locally administered unicast address.
    void choose_address(const sys::array<uint8_t, dwc_eqos::MAC_BYTES>* board_mac) {
        if (board_mac && usable(*board_mac)) {
            mac_ = *board_mac;
            return;
        }
        const uint32_t low = mmio::read32(base_, reg::MAC_ADDRESS0_LOW);
        const uint32_t high = mmio::read32(base_, reg::MAC_ADDRESS0_HIGH);
        for (size_t i = 0; i < 4; ++i)
            mac_[i] = static_cast<uint8_t>(low >> (8 * i));
        mac_[4] = static_cast<uint8_t>(high);
        mac_[5] = static_cast<uint8_t>(high >> 8);
        if (!usable(mac_)) {
            mac_ = {0x02, 0x00, 0x5A, 0x6F, 0x6E, 0x01};
            klog::warn("dwc_eqos: no address from firmware, using a fixed one\n");
        }
    }

    Error allocate() {
        rx_ring_ = TRY(DmaBuffer::alloc(RX_COUNT * sizeof(Descriptor)));
        tx_ring_ = TRY(DmaBuffer::alloc(TX_COUNT * sizeof(Descriptor)));
        rx_buffers_ = TRY(DmaBuffer::alloc(RX_COUNT * BUFFER_BYTES));
        tx_buffers_ = TRY(DmaBuffer::alloc(TX_COUNT * BUFFER_BYTES));
        return Error::None;
    }

    void release() {
        rx_ring_ = DmaBuffer{};
        tx_ring_ = DmaBuffer{};
        rx_buffers_ = DmaBuffer{};
        tx_buffers_ = DmaBuffer{};
        base_ = 0;
    }

    void program() {
        mmio::write32(base_, reg::DMA_SYSBUS_MODE,
                      SYSBUS_FIXED_BURST | SYSBUS_BLEN4 | SYSBUS_BLEN8 | SYSBUS_BLEN16 |
                          (SYSBUS_OUTSTANDING << SYSBUS_READ_LIMIT_SHIFT) |
                          (SYSBUS_OUTSTANDING << SYSBUS_WRITE_LIMIT_SHIFT));
        mmio::write32(base_, reg::DMA_CH0_TX_BASE_HIGH, static_cast<uint32_t>(tx_ring_.pa() >> 32));
        mmio::write32(base_, reg::DMA_CH0_TX_BASE_LOW, static_cast<uint32_t>(tx_ring_.pa()));
        mmio::write32(base_, reg::DMA_CH0_TX_RING_LENGTH, TX_COUNT - 1);
        mmio::write32(base_, reg::DMA_CH0_TX_TAIL, static_cast<uint32_t>(tx_ring_.pa()));
        mmio::write32(base_, reg::DMA_CH0_RX_BASE_HIGH, static_cast<uint32_t>(rx_ring_.pa() >> 32));
        mmio::write32(base_, reg::DMA_CH0_RX_BASE_LOW, static_cast<uint32_t>(rx_ring_.pa()));
        mmio::write32(base_, reg::DMA_CH0_RX_RING_LENGTH, RX_COUNT - 1);
        for (size_t line = 0; line < RX_COUNT / PER_LINE; ++line)
            refill(line);

        const uint32_t high = mac_[4] | (static_cast<uint32_t>(mac_[5]) << 8) | ADDRESS_ENABLE;
        const uint32_t low = mac_[0] | (static_cast<uint32_t>(mac_[1]) << 8) | (static_cast<uint32_t>(mac_[2]) << 16) |
                             (static_cast<uint32_t>(mac_[3]) << 24);
        mmio::write32(base_, reg::MAC_ADDRESS0_HIGH, high);
        mmio::write32(base_, reg::MAC_ADDRESS0_LOW, low);
        // Own unicast and broadcast frames, and every multicast one.
        mmio::write32(base_, reg::MAC_PACKET_FILTER, FILTER_PASS_ALL_MULTICAST);

        mmio::write32(base_, reg::MTL_TXQ0_OPERATION_MODE,
                      TXQ_STORE_FORWARD | TXQ_ENABLE | (QUEUE_SIZE << TXQ_SIZE_SHIFT));
        mmio::write32(base_, reg::MTL_RXQ0_OPERATION_MODE,
                      RXQ_STORE_FORWARD | RXQ_FORWARD_UNDERSIZED | (QUEUE_SIZE << RXQ_SIZE_SHIFT));
        // The receive queue passes nothing until it is enabled.
        mmio::write32(base_, reg::MAC_RXQ_CTRL0, RXQ0_ENABLE_DCB);

        mmio::write32(base_, reg::DMA_CH0_TX_CONTROL,
                      (BURST_LENGTH << BURST_SHIFT) | TX_OPERATE_ON_SECOND | CHANNEL_START);
        mmio::write32(base_, reg::DMA_CH0_RX_CONTROL,
                      (BURST_LENGTH << BURST_SHIFT) | (BUFFER_BYTES << RX_BUFFER_SIZE_SHIFT) | CHANNEL_START);
        const uint32_t config = mmio::read32(base_, reg::MAC_CONFIGURATION);
        mmio::write32(base_, reg::MAC_CONFIGURATION, config | MAC_ACS | MAC_CST | MAC_TE | MAC_RE);
    }

    // Hands the descriptors of one cache line back to the controller.
    void refill(size_t line) {
        const size_t first = line * PER_LINE;
        rx_buffers_.sync_for_device(first * BUFFER_BYTES, PER_LINE * BUFFER_BYTES);
        for (size_t index = first; index < first + PER_LINE; ++index) {
            const uintptr_t buffer = rx_buffers_.pa() + index * BUFFER_BYTES;
            Descriptor& descriptor = rx_descriptor(index);
            descriptor.word[0] = static_cast<uint32_t>(buffer);
            descriptor.word[1] = static_cast<uint32_t>(buffer >> 32);
            descriptor.word[2] = 0;
            descriptor.word[3] = DESC_OWN | RDES3_INTERRUPT | RDES3_BUFFER1_VALID;
        }
        rx_ring_.sync_for_device(first * sizeof(Descriptor), CACHE_LINE);
        mmio::write32(base_, reg::DMA_CH0_RX_TAIL,
                      static_cast<uint32_t>(rx_ring_.pa() + (first + PER_LINE - 1) * sizeof(Descriptor)));
    }

    bool received() {
        rx_ring_.sync_for_cpu(rx_next_ / PER_LINE * CACHE_LINE, CACHE_LINE);
        return (rx_descriptor(rx_next_).word[3] & DESC_OWN) == 0;
    }

    bool transmitting() {
        tx_ring_.sync_for_cpu(0, TX_COUNT * sizeof(Descriptor));
        const size_t previous = (tx_next_ + TX_COUNT - 1) % TX_COUNT;
        return (tx_descriptor(previous).word[3] & DESC_OWN) != 0;
    }

    void consume(size_t index) {
        rx_next_ = (index + 1) % RX_COUNT;
        if (index % PER_LINE == PER_LINE - 1)
            refill(index / PER_LINE);
    }

    Error check_io() const {
        ENSURE(state_ == State::Ready, Error::NoDevice);
        ENSURE(!sched::current()->termination_requested(), Error::Interrupted);
        return Error::None;
    }

    Result<uint32_t> mdio_read(uint32_t address, uint32_t reg_number) {
        mmio::write32(base_, reg::MAC_MDIO_ADDRESS,
                      MDIO_CLOCK_RANGE | (address << MDIO_PHY_SHIFT) | (reg_number << MDIO_REGISTER_SHIFT) | MDIO_READ |
                          MDIO_BUSY);
        for (int polls = 0; polls < POLL_LIMIT; ++polls) {
            if ((mmio::read32(base_, reg::MAC_MDIO_ADDRESS) & MDIO_BUSY) == 0)
                return mmio::read32(base_, reg::MAC_MDIO_DATA) & MDIO_DATA_MASK;
            arch_spin_hint();
        }
        return Error::Timeout;
    }

    // The board's PHY, or ADDRESSES when none answers.
    uint32_t find_phy() {
        for (uint32_t address = 0; address < phy::ADDRESSES; ++address) {
            auto id = mdio_read(address, phy::ID1);
            if (id.ok() && id.value() != phy::ABSENT && id.value() != 0)
                return address;
        }
        return phy::ADDRESSES;
    }

    // Matches the MAC to what the PHY negotiated; the status register holds a
    // lost link until it is read, so it is read twice.
    void update_link() {
        if (phy_ == phy::ADDRESSES)
            return;
        static_cast<void>(mdio_read(phy_, phy::STATUS));
        auto status = mdio_read(phy_, phy::STATUS);
        if (!status.ok())
            return;
        const bool up = (status.value() & phy::STATUS_LINK) != 0;
        uint32_t speed_bits = 0;
        int megabits = 0;
        if (up) {
            auto gigabit_control = mdio_read(phy_, phy::GIGABIT_CONTROL);
            auto gigabit_status = mdio_read(phy_, phy::GIGABIT_STATUS);
            auto advertise = mdio_read(phy_, phy::ADVERTISE);
            auto partner = mdio_read(phy_, phy::PARTNER);
            if (!gigabit_control.ok() || !gigabit_status.ok() || !advertise.ok() || !partner.ok())
                return;
            const uint32_t common = advertise.value() & partner.value();
            if ((gigabit_control.value() & phy::ADVERTISE_1000_FULL) != 0 &&
                (gigabit_status.value() & phy::PARTNER_1000_FULL) != 0) {
                speed_bits = MAC_DM;
                megabits = 1000;
            } else if ((gigabit_control.value() & phy::ADVERTISE_1000_HALF) != 0 &&
                       (gigabit_status.value() & phy::PARTNER_1000_HALF) != 0) {
                megabits = 1000;
            } else if ((common & phy::ABILITY_100_FULL) != 0) {
                speed_bits = MAC_PS | MAC_FES | MAC_DM;
                megabits = 100;
            } else if ((common & phy::ABILITY_100_HALF) != 0) {
                speed_bits = MAC_PS | MAC_FES;
                megabits = 100;
            } else if ((common & phy::ABILITY_10_FULL) != 0) {
                speed_bits = MAC_PS | MAC_DM;
                megabits = 10;
            } else {
                speed_bits = MAC_PS;
                megabits = 10;
            }
        }
        if (up == link_up_ && speed_bits == speed_bits_)
            return;
        link_up_ = up;
        speed_bits_ = speed_bits;
        if (!up) {
            klog::info("dwc_eqos: link down\n");
            return;
        }
        const uint32_t config = mmio::read32(base_, reg::MAC_CONFIGURATION) & ~MAC_SPEED_MASK;
        mmio::write32(base_, reg::MAC_CONFIGURATION, config | speed_bits);
        klog::info("dwc_eqos: link up, %d Mb/s %s duplex\n", megabits, (speed_bits & MAC_DM) != 0 ? "full" : "half");
    }

    uintptr_t base_{};
    DmaBuffer rx_ring_{};
    DmaBuffer tx_ring_{};
    DmaBuffer rx_buffers_{};
    DmaBuffer tx_buffers_{};
    sys::array<uint8_t, NET_MAC_SIZE> mac_{};
    WaitQueue progress_{};
    uint64_t link_poll_ms_{};
    size_t rx_next_{};
    size_t tx_next_{};
    uint32_t phy_{phy::ADDRESSES};
    uint32_t speed_bits_{};
    State state_{};
    bool link_up_{};
    bool opened_{};
    bool rx_announced_{};
    bool interrupts_stopped_{};
    bool storm_reported_{};
    uint32_t empty_interrupts_{};
};

class NetFile : public vfs::File {
public:
    explicit NetFile(Device& device) : device_(device) {}
    ~NetFile() override { device_.release_file(); }
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
    static Error attach(uintptr_t base, const uint8_t* mac, int irq) {
        ENSURE(base_ == 0, Error::Busy);
        TRY(vfs::register_char_dev("net0", open));
        base_ = base;
        irq_ = irq;
        board_mac_known_ = mac != nullptr;
        if (mac)
            memcpy(board_mac_.data(), mac, board_mac_.size());
        klog::info("net: DesignWare Ethernet at /dev/net0, started when opened\n");
        return Error::None;
    }

    static void tick() { device_.tick(); }

private:
    // The controller is touched only once a network service opens it, so a
    // fault in it cannot stop the system from booting.
    static Result<vfs::FileHandle> open() {
        if (!device_.started())
            TRY(device_.start(base_, board_mac_known_ ? &board_mac_ : nullptr, irq_));
        TRY(device_.acquire());
        vfs::FileHandle file(new (sys::nothrow) NetFile(device_));
        if (!file) {
            device_.release_file();
            return Error::NoMem;
        }
        return file;
    }

    inline static Device device_{};
    inline static uintptr_t base_{};
    inline static sys::array<uint8_t, dwc_eqos::MAC_BYTES> board_mac_{};
    inline static bool board_mac_known_{};
    inline static int irq_{};
};

}  // namespace

namespace dwc_eqos {

Error attach(uintptr_t base, const uint8_t* mac, int irq) {
    return Manager::attach(base, mac, irq);
}

void tick() {
    Manager::tick();
}

}  // namespace dwc_eqos
