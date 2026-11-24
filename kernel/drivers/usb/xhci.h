#pragma once

// xHCI (USB 3) host controller driver: one event ring polled from the timer
// tick, a command ring, and a transfer ring per endpoint in use. Control
// transfers and commands run in the USB kernel thread, which may sleep;
// interrupt IN reports complete from the tick.

#include <base/types.h>
#include "drivers/usb/usb.h"
#include "lib/result.h"
#include "mm/dma.h"
#include "mm/mmio.h"
#include <sys/array.hpp>

namespace pci {
struct DeviceInfo;
struct DriverId;
}  // namespace pci

namespace xhci {

inline constexpr size_t RING_TRBS = 256;
inline constexpr size_t MAX_SLOTS = 16;
inline constexpr size_t MAX_ENDPOINTS = 32;  // Device context indices
inline constexpr size_t REPORT_BUFFERS = 4;  // Interrupt IN requests kept queued

struct Trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};
static_assert(sizeof(Trb) == 16);

// A producer ring of TRBs ending in a link TRB back to its start.
class Ring {
public:
    Error allocate();
    // Writes the TRB with the producer cycle and returns its physical address.
    uintptr_t push(const Trb& trb);
    [[nodiscard]] uintptr_t pa() const { return buffer_.pa(); }

private:
    DmaBuffer buffer_{};
    size_t enqueue_{};
    bool cycle_{true};
};

struct Endpoint {
    Ring ring{};
    usb::ReportHandler handler{};
    void* context{};
    DmaBuffer reports{};
    uintptr_t trb_pa[REPORT_BUFFERS]{};  // The TRB queued for each report buffer
    uint16_t report_size{};
};

struct Slot {
    usb::Device device{};
    DmaBuffer output{};  // Device context the controller owns
    DmaBuffer input{};   // Input context for commands
    sys::array<Endpoint, MAX_ENDPOINTS> endpoints{};
    bool active{};
};

class Controller final : public usb::Host {
public:
    Error start(uintptr_t base);
    void enumerate_ports();
    void poll();

    Error control(usb::Device& device, const usb::SetupPacket& setup, void* data) override;
    Error start_interrupt_in(usb::Device& device, const usb::EndpointDescriptor& endpoint, usb::ReportHandler handler,
                             void* context) override;

private:
    struct Completion {
        bool done{};
        uint8_t code{};
        uint8_t slot{};
        uint32_t remaining{};
    };

    Error reset();
    void take_ownership();
    Error setup_scratchpad(uint32_t count);
    Error command(Trb trb, Completion& result);
    Error wait(Completion& completion, uint64_t timeout_ms);
    Error reset_port(uint8_t port, usb::Speed& speed);
    Error address_device(uint8_t port, usb::Speed speed);
    Error update_ep0_packet_size(Slot& slot, uint16_t packet_size);
    Slot* slot_of(usb::Device& device);
    uint32_t* input_context(Slot& slot, size_t index);
    void handle_event(const Trb& event);
    void queue_report(Slot& slot, uint8_t dci, size_t buffer);
    void ring_doorbell(uint8_t slot, uint8_t target);

    uintptr_t base_{};
    uintptr_t operational_{};
    uintptr_t runtime_{};
    uintptr_t doorbells_{};
    uint8_t max_ports_{};
    uint8_t max_slots_{};
    size_t context_size_{};
    DmaBuffer dcbaa_{};
    DmaBuffer scratchpad_array_{};
    DmaBuffer scratchpad_pages_{};
    Ring commands_{};
    DmaBuffer events_{};
    DmaBuffer event_segments_{};
    size_t event_dequeue_{};
    bool event_cycle_{true};
    DmaBuffer control_data_{};
    Completion command_done_{};
    Completion transfer_done_{};
    uintptr_t awaited_command_{};
    uintptr_t awaited_transfer_{};
    sys::array<Slot, MAX_SLOTS + 1> slots_{};  // Indexed by slot ID
};

class Manager {
public:
    static constexpr size_t MAX_CONTROLLERS = 2;

    static int init();
    static Error probe_callback(const pci::DeviceInfo* device, const pci::DriverId*);
    static int start_thread();
    static void tick();

private:
    static int thread(void*);

    inline static sys::array<Controller, MAX_CONTROLLERS> controllers_{};
    inline static sys::array<vmm::MmioRegion, MAX_CONTROLLERS> mappings_{};
    inline static size_t count_{};
    inline static bool running_{};
};

int init();
// Runs after the scheduler starts: brings up controllers and their devices.
int start_thread();
void tick();

}  // namespace xhci
