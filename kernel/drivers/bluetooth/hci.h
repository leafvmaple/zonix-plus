#pragma once

// Bluetooth host controller interface (HCI): the commands and events every
// controller speaks, whatever carries them. A transport driver (USB today)
// moves packets. A Bluetooth kernel thread brings each controller up, then
// looks for nearby devices with a classic inquiry and an LE scan and logs
// what it finds.

#include <base/types.h>
#include "lib/result.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>

namespace bluetooth {

// Carries HCI packets to and from one controller.
class Transport {
public:
    // Sends a command packet: opcode, parameter length, parameters. The
    // transport may use the packet as its transfer buffer.
    virtual Error send_command(uint8_t* packet, size_t length) = 0;

protected:
    ~Transport() = default;
};

inline constexpr size_t HCI_MAX_PARAMETERS = 255;
inline constexpr size_t ADDRESS_BYTES = 6;
inline constexpr size_t PEER_NAME_CAPACITY = 32;
inline constexpr size_t MAX_PEERS = 32;

// A device a scan has seen.
struct Peer {
    uint8_t address[ADDRESS_BYTES]{};  // Least significant byte first
    bool low_energy{};
    uint8_t address_type{};  // LE: public or random
    // Classic: class of device and what a name request needs.
    uint32_t device_class{};
    uint8_t page_scan_mode{};
    uint16_t clock_offset{};
    uint16_t appearance{};  // LE
    char name[PEER_NAME_CAPACITY]{};
};

// One controller the HCI layer has brought up.
struct Controller {
    Transport* transport{};  // Null while the entry is free
    uint8_t index{};
    uint32_t generation{};    // Tells a controller from a later one in the same entry
    bool bring_up_pending{};  // Waiting for the Bluetooth thread
    // The command being waited for and its return parameters, status first.
    uint16_t awaited_opcode{};
    bool replied{};
    uint8_t reply_length{};
    uint8_t reply[HCI_MAX_PARAMETERS]{};
    // An event a command started, such as the end of an inquiry.
    uint8_t awaited_event{};
    bool event_seen{};
    uint8_t event_length{};
    uint8_t event[HCI_MAX_PARAMETERS]{};
    sys::array<Peer, MAX_PEERS> peers{};
    size_t peer_count{};
};

// Parameters returned by a command or carried by an event.
struct Reply {
    uint8_t length{};
    uint8_t data[HCI_MAX_PARAMETERS]{};
};

class Hci {
public:
    static constexpr size_t MAX_CONTROLLERS = 2;
    static constexpr size_t MAX_PARAMETERS = HCI_MAX_PARAMETERS;

    // Takes on a controller that has just appeared, whose events already
    // flow; the Bluetooth thread resets it, logs what it is and scans. Runs
    // in a kernel thread.
    static Error attach(Transport& transport);
    // Forgets a controller whose transport is leaving.
    static void detach(Transport& transport);
    // One complete event from a controller: code, length, parameters. Any context.
    static void event_received(Transport& transport, const uint8_t* event, size_t length);
    // Timer tick: lets waits notice their timeout.
    static void tick();

private:
    static Error command(Controller& controller, uint16_t opcode, const uint8_t* parameters, uint8_t length,
                         Reply& reply);
    static void expect_event(Controller& controller, uint8_t code);
    static Error wait_event(Controller& controller, uint64_t timeout_ms, Reply& event);
    static Error identify(Controller& controller);
    static void bring_up(Controller& controller);
    static Error scan(Controller& controller);
    static void request_names(Controller& controller, uint32_t generation);

    static Controller* find(Transport& transport);
    static Peer* peer(Controller& controller, const uint8_t* address, bool low_energy);
    static void inquiry_result(Controller& controller, const uint8_t* address, uint8_t page_scan_mode,
                               const uint8_t* device_class, const uint8_t* clock_offset, const uint8_t* eir,
                               size_t eir_length);
    static void advertising_reports(Controller& controller, const uint8_t* parameters, size_t length);
    static void log_peer(const Controller& controller, const Peer& peer);

    [[noreturn]] static int thread(void*);
    static Controller* next_pending();

    inline static sys::array<Controller, MAX_CONTROLLERS> controllers_{};
    inline static uint32_t generations_{};
    inline static WaitQueue waiting_{};
    inline static WaitQueue pending_waiting_{};
    inline static bool thread_started_{};
};

}  // namespace bluetooth
