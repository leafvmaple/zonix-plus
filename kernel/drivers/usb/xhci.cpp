#include "drivers/usb/xhci.h"
#include "drivers/usb/usb_core.h"
#include "drivers/intr.h"
#include "drivers/mmio.h"
#include "drivers/pci.h"
#include "lib/memory.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <kernel/log.h>

#include <asm/arch.h>
#include <asm/page.h>

namespace xhci {
namespace {

// Capability registers.
constexpr uintptr_t CAP_LENGTH = 0x00;
constexpr uintptr_t CAP_HCSPARAMS1 = 0x04;
constexpr uintptr_t CAP_HCSPARAMS2 = 0x08;
constexpr uintptr_t CAP_HCCPARAMS1 = 0x10;
constexpr uintptr_t CAP_DBOFF = 0x14;
constexpr uintptr_t CAP_RTSOFF = 0x18;

// Operational registers.
constexpr uintptr_t OP_USBCMD = 0x00;
constexpr uintptr_t OP_USBSTS = 0x04;
constexpr uintptr_t OP_CRCR = 0x18;
constexpr uintptr_t OP_DCBAAP = 0x30;
constexpr uintptr_t OP_CONFIG = 0x38;
constexpr uintptr_t OP_PORTSC = 0x400;
constexpr uintptr_t PORT_STRIDE = 0x10;

// Interrupter 0 in the runtime registers.
constexpr uintptr_t RT_IMAN = 0x20;
constexpr uintptr_t RT_ERSTSZ = 0x28;
constexpr uintptr_t RT_ERSTBA = 0x30;
constexpr uintptr_t RT_ERDP = 0x38;

constexpr uint32_t USBCMD_RUN = 1U << 0;
constexpr uint32_t USBCMD_RESET = 1U << 1;
constexpr uint32_t USBSTS_HALTED = 1U << 0;
constexpr uint32_t USBSTS_NOT_READY = 1U << 11;
constexpr uint32_t HCC_CONTEXT_64 = 1U << 2;
constexpr uint64_t CRCR_CYCLE = 1;
constexpr uint64_t ERDP_BUSY = 1U << 3;

constexpr uint32_t PORT_CONNECTED = 1U << 0;
constexpr uint32_t PORT_ENABLED = 1U << 1;
constexpr uint32_t PORT_RESET = 1U << 4;
constexpr uint32_t PORT_CONNECT_CHANGE = 1U << 17;
constexpr uint32_t PORT_RESET_CHANGE = 1U << 21;
constexpr uint32_t PORT_CHANGE_BITS = 0x7FU << 17;  // Write 1 to clear
constexpr uint32_t PORT_SPEED_SHIFT = 10;
constexpr uint32_t PORT_SPEED_MASK = 0xF;

// USB legacy support capability: firmware hands the controller to the OS.
constexpr uint32_t XCAP_LEGACY = 1;
constexpr uint32_t LEGACY_BIOS_OWNED = 1U << 16;
constexpr uint32_t LEGACY_OS_OWNED = 1U << 24;

// TRB control fields.
constexpr uint32_t TRB_CYCLE = 1U << 0;
constexpr uint32_t TRB_TOGGLE_CYCLE = 1U << 1;
constexpr uint32_t TRB_ISP = 1U << 2;
constexpr uint32_t TRB_IOC = 1U << 5;
constexpr uint32_t TRB_IDT = 1U << 6;
constexpr uint32_t TRB_DIR_IN = 1U << 16;
constexpr uint32_t TRB_TYPE_SHIFT = 10;
constexpr uint32_t TRB_TYPE_MASK = 0x3F;
constexpr uint32_t TRT_OUT = 2U << 16;
constexpr uint32_t TRT_IN = 3U << 16;
constexpr uint32_t TRB_SLOT_SHIFT = 24;
constexpr uint32_t TRB_ENDPOINT_SHIFT = 16;
constexpr uint32_t TRB_ENDPOINT_MASK = 0x1F;

// TRB types.
constexpr uint32_t TRB_NORMAL = 1;
constexpr uint32_t TRB_SETUP = 2;
constexpr uint32_t TRB_DATA = 3;
constexpr uint32_t TRB_STATUS = 4;
constexpr uint32_t TRB_LINK = 6;
constexpr uint32_t TRB_ENABLE_SLOT = 9;
constexpr uint32_t TRB_ADDRESS_DEVICE = 11;
constexpr uint32_t TRB_CONFIGURE_ENDPOINT = 12;
constexpr uint32_t TRB_EVALUATE_CONTEXT = 13;
constexpr uint32_t TRB_TRANSFER_EVENT = 32;
constexpr uint32_t TRB_COMMAND_COMPLETION = 33;
constexpr uint32_t TRB_PORT_STATUS_CHANGE = 34;
constexpr uint32_t TRB_DISABLE_SLOT = 10;

constexpr uint8_t COMPLETION_SUCCESS = 1;
constexpr uint8_t COMPLETION_SHORT_PACKET = 13;

// Context fields.
constexpr uint32_t SLOT_SPEED_SHIFT = 20;
constexpr uint32_t SLOT_ENTRIES_SHIFT = 27;
constexpr uint32_t SLOT_ENTRIES_MASK = 0x1FU << SLOT_ENTRIES_SHIFT;
constexpr uint32_t SLOT_PORT_SHIFT = 16;
constexpr uint32_t SLOT_ROUTE_MASK = 0xFFFFF;
constexpr uint32_t SLOT_MULTI_TT = 1U << 25;
constexpr uint32_t SLOT_HUB = 1U << 26;
constexpr uint32_t SLOT_HUB_PORTS_SHIFT = 24;
constexpr uint32_t SLOT_TT_PORT_SHIFT = 8;
constexpr uint32_t SLOT_TT_THINK_SHIFT = 16;
constexpr uint32_t EP_ERROR_COUNT_3 = 3U << 1;
constexpr uint32_t EP_TYPE_SHIFT = 3;
constexpr uint32_t EP_TYPE_CONTROL = 4;
constexpr uint32_t EP_TYPE_INTERRUPT_IN = 7;
constexpr uint32_t EP_PACKET_SHIFT = 16;
constexpr uint32_t EP_INTERVAL_SHIFT = 16;
constexpr uint64_t EP_DEQUEUE_CYCLE = 1;
constexpr uint32_t CONTROL_DCI = 1;

constexpr size_t CONTROL_DATA_BYTES = PG_SIZE;
constexpr size_t REPORT_STRIDE = 64;
constexpr uint64_t COMMAND_TIMEOUT_MS = 1000;
constexpr uint64_t TRANSFER_TIMEOUT_MS = 1000;
constexpr uint64_t PORT_RESET_TIMEOUT_MS = 500;
constexpr uint64_t RESET_RECOVERY_MS = 20;
// A newly connected device must stay connected this long before its reset.
constexpr uint64_t DEBOUNCE_MS = 100;
constexpr uint32_t PORT_EVENT_PORT_SHIFT = 24;
constexpr int REGISTER_POLLS = 10000000;  // Poll budget; not a wall-clock duration

void write64(uintptr_t base, uintptr_t offset, uint64_t value) {
    mmio::write32(base, offset, static_cast<uint32_t>(value));
    mmio::write32(base, offset + 4, static_cast<uint32_t>(value >> 32));
}

template<typename Ready>
bool poll_until(Ready&& ready) {
    for (int i = 0; i < REGISTER_POLLS; ++i) {
        if (ready())
            return true;
        arch_spin_hint();
    }
    return ready();
}

usb::Speed port_speed(uint32_t portsc) {
    switch ((portsc >> PORT_SPEED_SHIFT) & PORT_SPEED_MASK) {
        case 1: return usb::Speed::Full;
        case 2: return usb::Speed::Low;
        case 3: return usb::Speed::High;
        case 4: return usb::Speed::Super;
        default: return usb::Speed::Unknown;
    }
}

uint32_t slot_speed(usb::Speed speed) {
    switch (speed) {
        case usb::Speed::Full: return 1;
        case usb::Speed::Low: return 2;
        case usb::Speed::High: return 3;
        case usb::Speed::Super: return 4;
        case usb::Speed::Unknown: break;
    }
    return 0;
}

uint16_t default_ep0_packet(usb::Speed speed) {
    switch (speed) {
        case usb::Speed::High: return 64;
        case usb::Speed::Super: return 512;
        default: return 8;
    }
}

// Interrupt endpoint period in the controller's 2^n x 125 us units.
uint32_t interrupt_interval(usb::Speed speed, uint8_t interval) {
    if (speed == usb::Speed::High || speed == usb::Speed::Super) {
        const uint32_t exponent = interval == 0 ? 1 : (interval > 16 ? 16 : interval);
        return exponent - 1;
    }
    uint32_t frames = interval == 0 ? 1 : interval;  // Milliseconds
    uint32_t units = 3;                              // 8 x 125 us = 1 ms
    while (units < 10 && (2U << units) <= frames * 8)
        ++units;
    return units;
}

bool completed(uint8_t code) {
    return code == COMPLETION_SUCCESS || code == COMPLETION_SHORT_PACKET;
}

}  // namespace

Error Ring::allocate() {
    // A spare page after the ring: some controllers (VIA VL805) fetch TRBs
    // beyond the link TRB at its end.
    buffer_ = TRY(DmaBuffer::alloc(RING_TRBS * sizeof(Trb) + PG_SIZE));
    auto* trbs = static_cast<Trb*>(buffer_.data());
    trbs[RING_TRBS - 1] = {buffer_.pa(), 0, (TRB_LINK << TRB_TYPE_SHIFT) | TRB_TOGGLE_CYCLE};
    buffer_.sync_for_device(0, buffer_.size());
    enqueue_ = 0;
    cycle_ = true;
    return Error::None;
}

uintptr_t Ring::push(const Trb& trb) {
    auto* trbs = static_cast<Trb*>(buffer_.data());
    Trb& slot = trbs[enqueue_];
    slot.parameter = trb.parameter;
    slot.status = trb.status;
    arch_mb();
    // The cycle bit hands the TRB to the controller, so it is written last.
    slot.control = (trb.control & ~TRB_CYCLE) | (cycle_ ? TRB_CYCLE : 0);
    buffer_.sync_for_device(enqueue_ * sizeof(Trb), sizeof(Trb));
    const uintptr_t written = buffer_.pa() + enqueue_ * sizeof(Trb);
    if (++enqueue_ == RING_TRBS - 1) {
        Trb& link = trbs[RING_TRBS - 1];
        link.control = (link.control & ~TRB_CYCLE) | (cycle_ ? TRB_CYCLE : 0);
        buffer_.sync_for_device((RING_TRBS - 1) * sizeof(Trb), sizeof(Trb));
        enqueue_ = 0;
        cycle_ = !cycle_;
    }
    return written;
}

void Controller::take_ownership() {
    uintptr_t offset = (mmio::read32(base_, CAP_HCCPARAMS1) >> 16) * sizeof(uint32_t);
    while (offset != 0) {
        const uint32_t capability = mmio::read32(base_, offset);
        if ((capability & 0xFF) == XCAP_LEGACY) {
            if ((capability & LEGACY_BIOS_OWNED) != 0) {
                mmio::write32(base_, offset, capability | LEGACY_OS_OWNED);
                if (!poll_until([&] { return (mmio::read32(base_, offset) & LEGACY_BIOS_OWNED) == 0; }))
                    klog::warn("xhci: firmware did not release the controller\n");
            }
            mmio::write32(base_, offset + 4, 0);  // No firmware SMIs
            return;
        }
        const uint32_t next = (capability >> 8) & 0xFF;
        offset = next == 0 ? 0 : offset + next * sizeof(uint32_t);
    }
}

Error Controller::reset() {
    mmio::write32(operational_, OP_USBCMD, mmio::read32(operational_, OP_USBCMD) & ~USBCMD_RUN);
    ENSURE_LOG(poll_until([&] { return (mmio::read32(operational_, OP_USBSTS) & USBSTS_HALTED) != 0; }), Error::Timeout,
               "xhci: controller did not halt");
    mmio::write32(operational_, OP_USBCMD, USBCMD_RESET);
    ENSURE_LOG(poll_until([&] {
                   return (mmio::read32(operational_, OP_USBCMD) & USBCMD_RESET) == 0 &&
                          (mmio::read32(operational_, OP_USBSTS) & USBSTS_NOT_READY) == 0;
               }),
               Error::Timeout, "xhci: controller reset did not finish");
    return Error::None;
}

Error Controller::setup_scratchpad(uint32_t count) {
    if (count == 0)
        return Error::None;
    scratchpad_array_ = TRY(DmaBuffer::alloc(count * sizeof(uint64_t)));
    scratchpad_pages_ = TRY(DmaBuffer::alloc(count * PG_SIZE));
    auto* pointers = static_cast<uint64_t*>(scratchpad_array_.data());
    for (uint32_t i = 0; i < count; ++i)
        pointers[i] = scratchpad_pages_.pa() + i * PG_SIZE;
    scratchpad_array_.sync_for_device(0, scratchpad_array_.size());
    static_cast<uint64_t*>(dcbaa_.data())[0] = scratchpad_array_.pa();
    return Error::None;
}

Error Controller::start(uintptr_t base) {
    base_ = base;
    operational_ = base + mmio::read8(base, CAP_LENGTH);
    runtime_ = base + (mmio::read32(base, CAP_RTSOFF) & ~0x1FU);
    doorbells_ = base + (mmio::read32(base, CAP_DBOFF) & ~0x3U);
    const uint32_t hcs1 = mmio::read32(base, CAP_HCSPARAMS1);
    const uint32_t hcs2 = mmio::read32(base, CAP_HCSPARAMS2);
    max_slots_ = static_cast<uint8_t>((hcs1 & 0xFF) < MAX_SLOTS ? (hcs1 & 0xFF) : MAX_SLOTS);
    max_ports_ = static_cast<uint8_t>(hcs1 >> 24);
    context_size_ = (mmio::read32(base, CAP_HCCPARAMS1) & HCC_CONTEXT_64) != 0 ? 64 : 32;

    take_ownership();
    TRY(reset());
    mmio::write32(operational_, OP_CONFIG, max_slots_);

    dcbaa_ = TRY(DmaBuffer::alloc((MAX_SLOTS + 1) * sizeof(uint64_t)));
    TRY(setup_scratchpad((((hcs2 >> 21) & 0x1F) << 5) | ((hcs2 >> 27) & 0x1F)));
    dcbaa_.sync_for_device(0, dcbaa_.size());
    write64(operational_, OP_DCBAAP, dcbaa_.pa());

    TRY(commands_.allocate());
    write64(operational_, OP_CRCR, commands_.pa() | CRCR_CYCLE);

    events_ = TRY(DmaBuffer::alloc(RING_TRBS * sizeof(Trb)));
    event_segments_ = TRY(DmaBuffer::alloc(4 * sizeof(uint64_t)));
    auto* segment = static_cast<uint64_t*>(event_segments_.data());
    segment[0] = events_.pa();
    segment[1] = RING_TRBS;
    event_segments_.sync_for_device(0, event_segments_.size());
    event_dequeue_ = 0;
    event_cycle_ = true;
    mmio::write32(runtime_, RT_ERSTSZ, 1);
    write64(runtime_, RT_ERDP, events_.pa());
    write64(runtime_, RT_ERSTBA, event_segments_.pa());
    mmio::write32(runtime_, RT_IMAN, 0);  // Events are polled, not interrupts

    control_data_ = TRY(DmaBuffer::alloc(CONTROL_DATA_BYTES));
    mmio::write32(operational_, OP_USBCMD, USBCMD_RUN);
    ENSURE_LOG(poll_until([&] { return (mmio::read32(operational_, OP_USBSTS) & USBSTS_HALTED) == 0; }), Error::Timeout,
               "xhci: controller did not start");
    klog::info("xhci: %u ports, %u slots\n", max_ports_, max_slots_);
    return Error::None;
}

void Controller::ring_doorbell(uint8_t slot, uint8_t target) {
    arch_mb();
    mmio::write32(doorbells_, slot * sizeof(uint32_t), target);
}

void Controller::poll() {
    intr::Guard guard;
    if (events_.empty())
        return;
    auto* events = static_cast<const Trb*>(events_.data());
    bool advanced = false;
    for (;;) {
        events_.sync_for_cpu(event_dequeue_ * sizeof(Trb), sizeof(Trb));
        const Trb event = events[event_dequeue_];
        if (((event.control & TRB_CYCLE) != 0) != event_cycle_)
            break;
        handle_event(event);
        advanced = true;
        if (++event_dequeue_ == RING_TRBS) {
            event_dequeue_ = 0;
            event_cycle_ = !event_cycle_;
        }
    }
    if (advanced)
        write64(runtime_, RT_ERDP, (events_.pa() + event_dequeue_ * sizeof(Trb)) | ERDP_BUSY);
}

void Controller::handle_event(const Trb& event) {
    const uint32_t type = (event.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK;
    const auto code = static_cast<uint8_t>(event.status >> 24);
    const auto slot = static_cast<uint8_t>(event.control >> TRB_SLOT_SHIFT);
    if (type == TRB_COMMAND_COMPLETION) {
        if (event.parameter == awaited_command_)
            command_done_ = {true, code, slot, 0};
        return;
    }
    if (type == TRB_PORT_STATUS_CHANGE) {
        usb::Core::port_changed(*this, 0, static_cast<uint8_t>(event.parameter >> PORT_EVENT_PORT_SHIFT));
        return;
    }
    if (type != TRB_TRANSFER_EVENT)
        return;
    const uint32_t remaining = event.status & 0xFFFFFF;
    if (event.parameter == awaited_transfer_) {
        transfer_done_ = {true, code, slot, remaining};
        return;
    }
    const uint32_t dci = (event.control >> TRB_ENDPOINT_SHIFT) & TRB_ENDPOINT_MASK;
    if (slot == 0 || slot > max_slots_ || !slots_[slot].active)
        return;
    Endpoint& endpoint = slots_[slot].endpoints[dci];
    if (!endpoint.handler || endpoint.reports.empty())
        return;
    // The event names the TRB; each queued TRB carries its buffer's address,
    // and buffers follow one another, so the position matches the request.
    for (size_t buffer = 0; buffer < REPORT_BUFFERS; ++buffer) {
        if (endpoint.trb_pa[buffer] != event.parameter)
            continue;
        if (!completed(code)) {
            klog::warn("xhci: slot %u endpoint %u stopped (completion %u)\n", slot, dci, code);
            endpoint.handler = nullptr;
            return;
        }
        const size_t length = remaining < endpoint.report_size ? endpoint.report_size - remaining : 0;
        endpoint.reports.sync_for_cpu(buffer * REPORT_STRIDE, endpoint.report_size);
        endpoint.handler(endpoint.context,
                         static_cast<const uint8_t*>(endpoint.reports.data()) + buffer * REPORT_STRIDE, length);
        queue_report(slots_[slot], static_cast<uint8_t>(dci), buffer);
        return;
    }
}

void Controller::queue_report(Slot& slot, uint8_t dci, size_t buffer) {
    Endpoint& endpoint = slot.endpoints[dci];
    endpoint.trb_pa[buffer] = endpoint.ring.push({endpoint.reports.pa() + buffer * REPORT_STRIDE, endpoint.report_size,
                                                  (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC | TRB_ISP});
    ring_doorbell(static_cast<uint8_t>(&slot - slots_.data()), dci);
}

Error Controller::wait(Completion& completion, uint64_t timeout_ms) {
    const uint64_t deadline = trap::uptime_ms() + timeout_ms;
    for (;;) {
        poll();
        {
            intr::Guard guard;
            if (completion.done)
                return Error::None;
        }
        if (trap::uptime_ms() >= deadline)
            return Error::Timeout;
        usb::Core::sleep_ms(1);
    }
}

Error Controller::command(Trb trb, Completion& result) {
    {
        intr::Guard guard;
        command_done_ = {};
        awaited_command_ = commands_.push(trb);
    }
    ring_doorbell(0, 0);
    TRY_LOG(wait(command_done_, COMMAND_TIMEOUT_MS), "xhci: command %u timed out",
            (trb.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK);
    intr::Guard guard;
    result = command_done_;
    awaited_command_ = 0;
    ENSURE_LOG(result.code == COMPLETION_SUCCESS, Error::Io, "xhci: command %u failed (completion %u)",
               (trb.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK, result.code);
    return Error::None;
}

uint32_t* Controller::input_context(Slot& slot, size_t index) {
    return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(slot.input.data()) + index * context_size_);
}

Error Controller::reset_port(uint8_t port, usb::Speed& speed) {
    const uintptr_t portsc = OP_PORTSC + (port - 1) * PORT_STRIDE;
    uint32_t status = mmio::read32(operational_, portsc);
    // USB 3 ports enable themselves after link training; USB 2 ports need a reset.
    if ((status & PORT_ENABLED) == 0) {
        mmio::write32(operational_, portsc, (status & ~(PORT_ENABLED | PORT_CHANGE_BITS)) | PORT_RESET);
        const uint64_t deadline = trap::uptime_ms() + PORT_RESET_TIMEOUT_MS;
        while ((mmio::read32(operational_, portsc) & PORT_RESET_CHANGE) == 0) {
            ENSURE(trap::uptime_ms() < deadline, Error::Timeout);
            usb::Core::sleep_ms(1);
        }
        status = mmio::read32(operational_, portsc);
        mmio::write32(operational_, portsc, (status & ~(PORT_ENABLED | PORT_CHANGE_BITS)) | PORT_RESET_CHANGE);
        usb::Core::sleep_ms(RESET_RECOVERY_MS);
        status = mmio::read32(operational_, portsc);
    }
    ENSURE((status & PORT_ENABLED) != 0 && (status & PORT_CONNECTED) != 0, Error::Io);
    speed = port_speed(status);
    return Error::None;
}

Error Controller::update_ep0_packet_size(Slot& slot, uint16_t packet_size) {
    memset(slot.input.data(), 0, slot.input.size());
    input_context(slot, 0)[1] = 1U << CONTROL_DCI;
    uint32_t* ep0 = input_context(slot, CONTROL_DCI + 1);
    ep0[1] =
        EP_ERROR_COUNT_3 | (EP_TYPE_CONTROL << EP_TYPE_SHIFT) | (static_cast<uint32_t>(packet_size) << EP_PACKET_SHIFT);
    slot.input.sync_for_device(0, slot.input.size());
    Completion done{};
    const auto id = static_cast<uint32_t>(&slot - slots_.data());
    return command({slot.input.pa(), 0, (TRB_EVALUATE_CONTEXT << TRB_TYPE_SHIFT) | (id << TRB_SLOT_SHIFT)}, done);
}

Error Controller::address_device(const usb::Topology& topology, uint8_t parent_slot, uint8_t port, usb::Speed speed) {
    Completion enabled{};
    TRY(command({0, 0, TRB_ENABLE_SLOT << TRB_TYPE_SHIFT}, enabled));
    ENSURE(enabled.slot != 0 && enabled.slot <= max_slots_, Error::Full);
    Slot& slot = slots_[enabled.slot];
    slot = Slot{};
    slot.output = TRY(DmaBuffer::alloc(context_size_ * MAX_ENDPOINTS));
    slot.input = TRY(DmaBuffer::alloc(context_size_ * (MAX_ENDPOINTS + 1)));
    TRY(slot.endpoints[CONTROL_DCI].ring.allocate());
    static_cast<uint64_t*>(dcbaa_.data())[enabled.slot] = slot.output.pa();
    dcbaa_.sync_for_device(enabled.slot * sizeof(uint64_t), sizeof(uint64_t));

    const uint16_t packet = default_ep0_packet(speed);
    input_context(slot, 0)[1] = (1U << 0) | (1U << CONTROL_DCI);
    uint32_t* slot_context = input_context(slot, 1);
    slot_context[0] = (topology.route & SLOT_ROUTE_MASK) | (slot_speed(speed) << SLOT_SPEED_SHIFT) |
                      (topology.multi_tt ? SLOT_MULTI_TT : 0) | (1U << SLOT_ENTRIES_SHIFT);
    slot_context[1] = static_cast<uint32_t>(topology.root_port) << SLOT_PORT_SHIFT;
    slot_context[2] = topology.tt_slot | (static_cast<uint32_t>(topology.tt_port) << SLOT_TT_PORT_SHIFT);
    uint32_t* ep0 = input_context(slot, CONTROL_DCI + 1);
    ep0[1] = EP_ERROR_COUNT_3 | (EP_TYPE_CONTROL << EP_TYPE_SHIFT) | (static_cast<uint32_t>(packet) << EP_PACKET_SHIFT);
    const uint64_t dequeue = slot.endpoints[CONTROL_DCI].ring.pa() | EP_DEQUEUE_CYCLE;
    ep0[2] = static_cast<uint32_t>(dequeue);
    ep0[3] = static_cast<uint32_t>(dequeue >> 32);
    ep0[4] = 8;  // Average TRB length for control transfers
    slot.input.sync_for_device(0, slot.input.size());
    Completion addressed{};
    TRY(command({slot.input.pa(), 0,
                 (TRB_ADDRESS_DEVICE << TRB_TYPE_SHIFT) | (static_cast<uint32_t>(enabled.slot) << TRB_SLOT_SHIFT)},
                addressed));

    slot.active = true;
    slot.device = usb::Device{this, enabled.slot, parent_slot, port, speed, topology, {}};
    // Full-speed devices state their endpoint 0 packet size in the first
    // eight bytes of the device descriptor.
    if (speed == usb::Speed::Full) {
        const usb::SetupPacket setup{usb::REQUEST_IN, usb::REQUEST_GET_DESCRIPTOR,
                                     static_cast<uint16_t>(usb::DESCRIPTOR_DEVICE << 8), 0, 8};
        TRY(control(slot.device, setup, &slot.device.descriptor));
        const uint8_t actual = slot.device.descriptor.max_packet_size0;
        if (actual != packet && actual != 0)
            TRY(update_ep0_packet_size(slot, actual));
    }
    return usb::Core::attach(slot.device);
}

void Controller::enumerate_ports() {
    for (uint8_t port = 1; port <= max_ports_; ++port)
        root_port_changed(port);
}

void Controller::root_port_changed(uint8_t port) {
    if (port == 0 || port > max_ports_)
        return;
    const uintptr_t portsc = OP_PORTSC + (port - 1) * PORT_STRIDE;
    uint32_t status = mmio::read32(operational_, portsc);
    // Acknowledge the changes; the port's current state decides what to do.
    mmio::write32(operational_, portsc, (status & ~(PORT_ENABLED | PORT_CHANGE_BITS)) | (status & PORT_CHANGE_BITS));
    usb::Device* child = this->child(0, port);
    const bool connected = (status & PORT_CONNECTED) != 0;
    if (child && (!connected || (status & PORT_CONNECT_CHANGE) != 0)) {
        remove_device(*child);
        child = nullptr;
    }
    if (!connected || child)
        return;
    usb::Core::sleep_ms(DEBOUNCE_MS);
    if ((mmio::read32(operational_, portsc) & PORT_CONNECTED) == 0)
        return;
    usb::Speed speed{};
    Error error = reset_port(port, speed);
    if (error == Error::None)
        error = address_device(usb::Topology{port, 0, 0, 0, 0, false}, 0, port, speed);
    if (error != Error::None)
        klog::warn("usb %u: device not usable (%d)\n", port, static_cast<int>(error));
}

void Controller::remove_device(usb::Device& device) {
    Slot* slot = slot_of(device);
    if (!slot)
        return;
    for (size_t id = 1; id <= max_slots_; ++id)
        if (slots_[id].active && slots_[id].device.parent_slot == device.slot)
            remove_device(slots_[id].device);
    usb::Core::detach(device);
    const uint8_t id = device.slot;
    {
        // No more reports reach class drivers once the slot is inactive.
        intr::Guard guard;
        slot->active = false;
        for (Endpoint& endpoint : slot->endpoints)
            endpoint.handler = nullptr;
    }
    Completion disabled{};
    static_cast<void>(command(
        {0, 0, (TRB_DISABLE_SLOT << TRB_TYPE_SHIFT) | (static_cast<uint32_t>(id) << TRB_SLOT_SHIFT)}, disabled));
    static_cast<uint64_t*>(dcbaa_.data())[id] = 0;
    dcbaa_.sync_for_device(id * sizeof(uint64_t), sizeof(uint64_t));
    *slot = Slot{};
}

usb::Device* Controller::child(uint8_t parent_slot, uint8_t port) {
    for (size_t id = 1; id <= max_slots_; ++id)
        if (slots_[id].active && slots_[id].device.parent_slot == parent_slot && slots_[id].device.port == port)
            return &slots_[id].device;
    return nullptr;
}

usb::Device* Controller::device(uint8_t slot) {
    return slot != 0 && slot <= max_slots_ && slots_[slot].active ? &slots_[slot].device : nullptr;
}

// Configure Endpoint with only the slot context added updates the hub
// fields the controller needs to schedule split transactions.
Error Controller::set_hub(usb::Device& hub, uint8_t ports, uint8_t think_time, bool multi_tt) {
    Slot* slot = slot_of(hub);
    ENSURE(slot, Error::NoDevice);
    slot->output.sync_for_cpu(0, context_size_);
    memset(slot->input.data(), 0, slot->input.size());
    input_context(*slot, 0)[1] = 1U << 0;
    uint32_t* slot_context = input_context(*slot, 1);
    memcpy(slot_context, slot->output.data(), context_size_);
    slot_context[0] |= SLOT_HUB | (multi_tt ? SLOT_MULTI_TT : 0);
    slot_context[1] =
        (slot_context[1] & ~(0xFFU << SLOT_HUB_PORTS_SHIFT)) | (static_cast<uint32_t>(ports) << SLOT_HUB_PORTS_SHIFT);
    slot_context[2] =
        (slot_context[2] & ~(3U << SLOT_TT_THINK_SHIFT)) | (static_cast<uint32_t>(think_time) << SLOT_TT_THINK_SHIFT);
    slot->input.sync_for_device(0, slot->input.size());
    Completion done{};
    TRY(command({slot->input.pa(), 0,
                 (TRB_CONFIGURE_ENDPOINT << TRB_TYPE_SHIFT) | (static_cast<uint32_t>(hub.slot) << TRB_SLOT_SHIFT)},
                done));
    slot->multi_tt = multi_tt;
    return Error::None;
}

Error Controller::add_device(usb::Device& hub, uint8_t port, usb::Speed speed) {
    Slot* parent = slot_of(hub);
    ENSURE(parent && port != 0 && port <= 15 && hub.topology.depth < usb::MAX_HUB_DEPTH, Error::Invalid);
    usb::Topology topology = hub.topology;
    topology.route |= static_cast<uint32_t>(port) << (4 * hub.topology.depth);
    topology.depth = static_cast<uint8_t>(hub.topology.depth + 1);
    // Low- and full-speed traffic below a high-speed hub goes through that
    // hub's transaction translator; deeper full-speed hubs share it.
    const bool slow = speed == usb::Speed::Low || speed == usb::Speed::Full;
    if (slow && hub.speed == usb::Speed::High) {
        topology.tt_slot = hub.slot;
        topology.tt_port = port;
        topology.multi_tt = parent->multi_tt;
    } else if (!slow) {
        topology.tt_slot = 0;
        topology.tt_port = 0;
        topology.multi_tt = false;
    }
    return address_device(topology, hub.slot, port, speed);
}

Slot* Controller::slot_of(usb::Device& device) {
    if (device.host != this || device.slot == 0 || device.slot > max_slots_ || !slots_[device.slot].active)
        return nullptr;
    return &slots_[device.slot];
}

Error Controller::control(usb::Device& device, const usb::SetupPacket& setup, void* data) {
    Slot* slot = slot_of(device);
    ENSURE(slot, Error::NoDevice);
    ENSURE(setup.length <= CONTROL_DATA_BYTES && (setup.length == 0 || data), Error::Invalid);
    const bool in = (setup.request_type & usb::REQUEST_IN) != 0;
    if (setup.length != 0 && !in) {
        memcpy(control_data_.data(), data, setup.length);
        control_data_.sync_for_device(0, setup.length);
    }
    uint64_t immediate{};
    memcpy(&immediate, &setup, sizeof(setup));
    Ring& ring = slot->endpoints[CONTROL_DCI].ring;
    uintptr_t status_trb{};
    {
        intr::Guard guard;
        const uint32_t transfer_type = setup.length == 0 ? 0 : (in ? TRT_IN : TRT_OUT);
        ring.push({immediate, sizeof(setup), (TRB_SETUP << TRB_TYPE_SHIFT) | TRB_IDT | transfer_type});
        if (setup.length != 0)
            ring.push({control_data_.pa(), setup.length, (TRB_DATA << TRB_TYPE_SHIFT) | (in ? TRB_DIR_IN : 0)});
        // The status stage runs opposite to the data stage, IN when there is none.
        const bool status_in = setup.length == 0 || !in;
        transfer_done_ = {};
        status_trb = ring.push({0, 0, (TRB_STATUS << TRB_TYPE_SHIFT) | TRB_IOC | (status_in ? TRB_DIR_IN : 0)});
        awaited_transfer_ = status_trb;
    }
    ring_doorbell(device.slot, CONTROL_DCI);
    Error error = wait(transfer_done_, TRANSFER_TIMEOUT_MS);
    intr::Guard guard;
    awaited_transfer_ = 0;
    TRY(error);
    ENSURE(completed(transfer_done_.code), Error::Io);
    if (setup.length != 0 && in) {
        control_data_.sync_for_cpu(0, setup.length);
        memcpy(data, control_data_.data(), setup.length);
    }
    return Error::None;
}

Error Controller::start_interrupt_in(usb::Device& device, const usb::EndpointDescriptor& descriptor,
                                     usb::ReportHandler handler, void* context) {
    Slot* slot = slot_of(device);
    ENSURE(slot && handler, Error::Invalid);
    const auto dci = static_cast<uint8_t>((descriptor.address & usb::ENDPOINT_NUMBER_MASK) * 2 + 1);
    Endpoint& endpoint = slot->endpoints[dci];
    const uint16_t packet = descriptor.max_packet_size & 0x7FF;
    ENSURE(packet != 0, Error::Invalid);
    TRY(endpoint.ring.allocate());
    endpoint.reports = TRY(DmaBuffer::alloc(REPORT_BUFFERS * REPORT_STRIDE));
    endpoint.report_size = packet < REPORT_STRIDE ? packet : REPORT_STRIDE;

    // Configure Endpoint copies the current slot context and adds this one.
    slot->output.sync_for_cpu(0, context_size_);
    memset(slot->input.data(), 0, slot->input.size());
    input_context(*slot, 0)[1] = (1U << 0) | (1U << dci);
    uint32_t* slot_context = input_context(*slot, 1);
    memcpy(slot_context, slot->output.data(), context_size_);
    const uint32_t entries = (slot_context[0] & SLOT_ENTRIES_MASK) >> SLOT_ENTRIES_SHIFT;
    if (dci > entries)
        slot_context[0] = (slot_context[0] & ~SLOT_ENTRIES_MASK) | (static_cast<uint32_t>(dci) << SLOT_ENTRIES_SHIFT);
    uint32_t* ep = input_context(*slot, dci + 1);
    ep[0] = interrupt_interval(device.speed, descriptor.interval) << EP_INTERVAL_SHIFT;
    ep[1] =
        EP_ERROR_COUNT_3 | (EP_TYPE_INTERRUPT_IN << EP_TYPE_SHIFT) | (static_cast<uint32_t>(packet) << EP_PACKET_SHIFT);
    const uint64_t dequeue = endpoint.ring.pa() | EP_DEQUEUE_CYCLE;
    ep[2] = static_cast<uint32_t>(dequeue);
    ep[3] = static_cast<uint32_t>(dequeue >> 32);
    ep[4] = packet | (static_cast<uint32_t>(packet) << 16);  // Average TRB length, max ESIT payload
    slot->input.sync_for_device(0, slot->input.size());
    Completion configured{};
    TRY(command({slot->input.pa(), 0,
                 (TRB_CONFIGURE_ENDPOINT << TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slot) << TRB_SLOT_SHIFT)},
                configured));

    intr::Guard guard;
    endpoint.context = context;
    endpoint.handler = handler;
    for (size_t buffer = 0; buffer < REPORT_BUFFERS; ++buffer)
        queue_report(*slot, dci, buffer);
    return Error::None;
}

namespace {

const pci::DriverId XHCI_IDS[] = {
    {pci::ANY_ID, pci::ANY_ID, pci::CLASS_SERIAL_BUS, pci::SUBCLASS_USB, pci::INTERFACE_XHCI},
};

const pci::Driver XHCI_DRIVER = {"xhci", XHCI_IDS, 1, Manager::probe_callback};

// Size of a memory BAR, found by writing all ones and reading back the mask.
uint64_t bar_size(const pci::DeviceInfo& device, int index, bool wide) {
    const int offset = pci::Bar0 + index * 4;
    const uint32_t low = pci::config_read32(device.bus, device.slot, device.function, offset);
    pci::config_write32(device.bus, device.slot, device.function, offset, 0xFFFFFFFF);
    uint64_t mask = pci::config_read32(device.bus, device.slot, device.function, offset) & ~0xFULL;
    pci::config_write32(device.bus, device.slot, device.function, offset, low);
    if (wide) {
        const uint32_t high = pci::config_read32(device.bus, device.slot, device.function, offset + 4);
        pci::config_write32(device.bus, device.slot, device.function, offset + 4, 0xFFFFFFFF);
        mask |= static_cast<uint64_t>(pci::config_read32(device.bus, device.slot, device.function, offset + 4)) << 32;
        pci::config_write32(device.bus, device.slot, device.function, offset + 4, high);
    } else {
        mask |= 0xFFFFFFFF00000000ULL;
    }
    return ~mask + 1;
}

}  // namespace

int Manager::init() {
    return static_cast<int>(pci::register_driver(&XHCI_DRIVER));
}

Error Manager::probe_callback(const pci::DeviceInfo* device, const pci::DriverId*) {
    ENSURE(device, Error::Invalid);
    ENSURE(count_ < controllers_.size(), Error::Full);
    const uint32_t low = pci::read_bar(device->bus, device->slot, device->function, 0);
    ENSURE((low & 1U) == 0, Error::NotSupported);
    const bool wide = (low & 6U) == 4U;
    uint64_t pa = low & ~0xFULL;
    if (wide)
        pa |= static_cast<uint64_t>(pci::read_bar(device->bus, device->slot, device->function, 1)) << 32;
    ENSURE_LOG(pa != 0, Error::Invalid, "xhci: %02x:%02x.%x has no memory BAR", device->bus, device->slot,
               device->function);
    uint64_t size{};
    {
        pci::CommandGuard decode_off(*device, 0);  // Size the BAR with decoding as it was
        size = bar_size(*device, 0, wide);
    }
    ENSURE_LOG(size >= PG_SIZE && size <= 0x100000, Error::Invalid, "xhci: unexpected BAR size 0x%lx", size);
    auto mapping = TRY(vmm::map_mmio(pa, size, VM_WRITE | VM_NOCACHE));
    pci::CommandGuard command(*device, pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER);
    command.commit();
    mappings_[count_] = sys::move(mapping);
    ++count_;
    klog::info("xhci: controller %02x:%02x.%x at 0x%lx\n", device->bus, device->slot, device->function, pa);
    return Error::None;
}

// Controllers start and devices enumerate in a kernel thread, where waits
// for resets and transfers may sleep; it then stays to follow port changes.
int Manager::thread(void*) {
    for (size_t i = 0; i < count_; ++i) {
        Controller& controller = controllers_[i];
        if (controller.start(mappings_[i]->address()) != Error::None)
            continue;
        {
            intr::Guard guard;
            running_ = true;
        }
        controller.enumerate_ports();
    }
    usb::Core::run();
}

int Manager::start_thread() {
    if (count_ == 0)
        return 0;
    auto pid = sched::kernel_thread(thread, nullptr);
    return pid.ok() ? 0 : static_cast<int>(pid.error());
}

void Manager::tick() {
    if (!running_)
        return;
    for (size_t i = 0; i < count_; ++i)
        controllers_[i].poll();
}

int init() {
    return Manager::init();
}

int start_thread() {
    return Manager::start_thread();
}

void tick() {
    Manager::tick();
}

}  // namespace xhci
