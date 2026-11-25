#pragma once

// Bluetooth host: the controller interface (HCI) every adapter speaks,
// whatever carries it, and the LE protocols above it that keyboards and mice
// need. A transport driver (USB today) moves packets. A Bluetooth kernel
// thread brings each controller up and scans for nearby devices. It then
// connects to every LE keyboard or mouse in pairing mode: it pairs (SMP
// legacy pairing, Just Works), finds the HID service over GATT and turns the
// device's boot reports into key and pointer events. Afterwards it keeps a
// low-duty scan running, which brings back bonded devices that dropped
// their link, as they do when idle, with the bonded key, and pairs devices
// put in pairing mode later. Bonds are kept in a file, so that devices come
// back after a restart too.
//
// hci.cpp holds the controller, its commands, events, scanning and the
// thread; hci_link.cpp connections, ACL data and L2CAP; hci_pairing.cpp
// SMP; hci_gatt.cpp the GATT client for the HID service; and hci_bonds.cpp
// the bond file.

#include <base/types.h>
#include "drivers/boot_input.h"
#include "lib/result.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>

namespace bluetooth {

// Carries HCI packets to and from one controller. The transport may use a
// packet as its transfer buffer.
class Transport {
public:
    // Sends a command packet: opcode, parameter length, parameters.
    virtual Error send_command(uint8_t* packet, size_t length) = 0;
    // Sends an ACL data packet: handle and flags, data length, data.
    virtual Error send_acl(uint8_t* packet, size_t length) = 0;

protected:
    ~Transport() = default;
};

inline constexpr size_t HCI_MAX_PARAMETERS = 255;
inline constexpr size_t ADDRESS_BYTES = 6;
inline constexpr size_t KEY_BYTES = 16;
inline constexpr size_t PEER_NAME_CAPACITY = 32;
inline constexpr size_t MAX_PEERS = 32;
inline constexpr size_t MAX_DEVICES = 4;
// L2CAP payloads kept for the thread: ATT with the default 23-byte MTU,
// SMP and signaling all fit.
inline constexpr size_t FRAME_CAPACITY = 64;
inline constexpr size_t FRAME_QUEUE = 8;
inline constexpr size_t MAX_REPORTS = 4;

// A device a scan has seen.
struct Peer {
    uint8_t address[ADDRESS_BYTES]{};  // Least significant byte first
    bool low_energy{};
    uint8_t address_type{};  // LE: public or random
    bool connectable{};      // LE: it accepts connections
    bool discoverable{};     // LE: it wants to be found, as in pairing mode
    uint8_t attempts{};      // Pairings tried with it
    // Classic: class of device and what a name request needs.
    uint32_t device_class{};
    uint8_t page_scan_mode{};
    uint16_t clock_offset{};
    uint16_t appearance{};  // LE
    char name[PEER_NAME_CAPACITY]{};
};

// An L2CAP frame received for the thread.
struct Frame {
    uint16_t channel{};
    uint8_t length{};
    uint8_t data[FRAME_CAPACITY]{};
};

// Where an input report's notifications go.
enum class ReportKind : uint8_t { Keyboard, Mouse };

// What pairing leaves for reconnecting: the key the device handed out and
// where its reports are.
struct Bond {
    bool valid{};
    uint8_t address[ADDRESS_BYTES]{};
    uint8_t address_type{};
    uint8_t ltk[KEY_BYTES]{};
    uint16_t ediv{};
    uint8_t rand[8]{};
    uint16_t protocol_mode{};         // Handle of Protocol Mode in boot mode, else 0
    uint16_t reports[MAX_REPORTS]{};  // Input report value handles
    uint16_t configs[MAX_REPORTS]{};  // Their client characteristic configurations
    ReportKind kinds[MAX_REPORTS]{};
    size_t report_count{};
    input::MouseLayout mouse_layout{};  // Valid when mouse reports use the report descriptor's layout
};

// An LE connection.
struct Link {
    bool connected{};
    uint16_t handle{};
    uint16_t in_flight{};  // ACL packets the controller still holds
    // The L2CAP frame being put together from ACL fragments: header, then payload.
    uint8_t assembly[4 + FRAME_CAPACITY]{};
    size_t assembled{};
    size_t expected{};  // 0: between frames
    sys::array<Frame, FRAME_QUEUE> frames{};
    size_t frame_count{};
    // Notifications from these attribute handles are input reports.
    uint16_t reports[MAX_REPORTS]{};
    ReportKind kinds[MAX_REPORTS]{};
    size_t report_count{};
    input::MouseLayout mouse_layout{};
    input::BootKeyboard keyboard{};
    input::Mouse mouse{};
};

// A keyboard or mouse being paired with, or bonded.
struct Device {
    bool used{};
    bool seen{};  // Advertised while disconnected
    uint16_t appearance{};
    Bond bond{};
    Link link{};
};

// One controller the HCI layer has brought up.
struct Controller {
    Transport* transport{};  // Null while the entry is free
    uint8_t index{};
    uint32_t generation{};    // Tells a controller from a later one in the same entry
    bool bring_up_pending{};  // Waiting for the Bluetooth thread
    uint8_t address[ADDRESS_BYTES]{};
    // LE ACL data: the largest packet and the controller's free buffers.
    uint16_t acl_mtu{};
    uint16_t acl_buffers{};
    uint16_t acl_credits{};
    // The command being waited for and its return parameters, status first.
    uint16_t awaited_opcode{};
    bool replied{};
    uint8_t reply_length{};
    uint8_t reply[HCI_MAX_PARAMETERS]{};
    // An event a command started, such as the end of an inquiry; for LE Meta
    // events, also its subevent.
    uint8_t awaited_event{};
    uint8_t awaited_subevent{};
    bool event_seen{};
    uint8_t event_length{};
    uint8_t event[HCI_MAX_PARAMETERS]{};
    sys::array<Peer, MAX_PEERS> peers{};
    size_t peer_count{};
    bool watching{};        // The background scan runs
    bool candidate_seen{};  // It found an input device in pairing mode
    sys::array<Device, MAX_DEVICES> devices{};
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
    // flow; the Bluetooth thread does the rest. Runs in a kernel thread.
    static Error attach(Transport& transport);
    // Forgets a controller whose transport is leaving.
    static void detach(Transport& transport);
    // One complete event from a controller: code, length, parameters. Any context.
    static void event_received(Transport& transport, const uint8_t* event, size_t length);
    // One complete ACL data packet from a controller. Any context.
    static void acl_received(Transport& transport, const uint8_t* packet, size_t length);
    // Timer tick: lets waits notice their timeout and repeats held keys.
    static void tick();

private:
    // hci.cpp: commands, events, scanning and the thread.
    static Error command(Controller& controller, uint16_t opcode, const uint8_t* parameters, uint8_t length,
                         Reply& reply);
    static void expect_event(Controller& controller, uint8_t code, uint8_t subevent = 0);
    static Error wait_event(Controller& controller, uint64_t timeout_ms, Reply& event);
    static Error identify(Controller& controller);
    static void bring_up(Controller& controller);
    static Error scan(Controller& controller);
    static void request_names(Controller& controller, uint32_t generation);
    static bool next_candidate(Controller& controller, Peer& candidate);
    static bool known(const Controller& controller, const uint8_t* address);
    static Error add_device(Controller& controller, const Peer& candidate);
    static Error resume(Controller& controller, Device& device);
    static void drop(Controller& controller, Device& device);

    static Controller* find(Transport& transport);
    static Peer* peer(Controller& controller, const uint8_t* address, bool low_energy, bool& added);
    static void inquiry_result(Controller& controller, const uint8_t* address, uint8_t page_scan_mode,
                               const uint8_t* device_class, const uint8_t* clock_offset, const uint8_t* eir,
                               size_t eir_length);
    static void advertising_reports(Controller& controller, const uint8_t* parameters, size_t length);
    static void log_peer(const Controller& controller, const Peer& peer);

    [[noreturn]] static int thread(void*);
    static Controller* next_pending();

    // hci_link.cpp: connections, ACL data and L2CAP.
    static Error connect(Controller& controller, Device& device, const uint8_t* address, uint8_t address_type);
    static Error watch(Controller& controller);
    static Error send_frame(Controller& controller, Device& device, uint16_t channel, const uint8_t* payload,
                            size_t length);
    static Error wait_frame(Controller& controller, Device* device, uint16_t channel, uint64_t timeout_ms,
                            Frame& frame);
    static bool answer_request(Controller& controller, Device& device, const Frame& frame);
    static Device* connected_device(Controller& controller, uint16_t handle);
    static void frame_received(Controller& controller, Link& link, uint16_t channel, const uint8_t* payload,
                               size_t length);
    static void link_lost(Controller& controller, uint16_t handle, uint8_t reason);
    static void packets_completed(Controller& controller, const uint8_t* parameters, size_t length);

    // hci_pairing.cpp: SMP legacy pairing and link encryption.
    static Error pair(Controller& controller, Device& device, const Peer& peer);
    static Error encrypt(Controller& controller, Device& device, const uint8_t* ltk, uint16_t ediv,
                         const uint8_t* rand);
    static Error aes(Controller& controller, const uint8_t* key, const uint8_t* plaintext, uint8_t* out);
    static Error confirm_value(Controller& controller, const uint8_t* random, const uint8_t* request,
                               const uint8_t* response, uint8_t initiator_type, const uint8_t* initiator,
                               uint8_t responder_type, const uint8_t* responder, uint8_t* out);
    static Error random(Controller& controller, uint8_t* out, size_t length);
    static void check_aes(Controller& controller);

    // hci_gatt.cpp: the GATT client for the HID service.
    static Error attribute_request(Controller& controller, Device& device, const uint8_t* request, size_t length,
                                   Frame& response);
    static Error read_long(Controller& controller, Device& device, uint16_t handle, uint8_t* value, size_t capacity,
                           size_t& length);
    static Error find_reports(Controller& controller, Device& device);
    static Error subscribe(Controller& controller, Device& device);

    // hci_bonds.cpp: bonds kept across restarts.
    static void load_bonds(Controller& controller);
    static void save_bonds(Controller& controller);

    inline static sys::array<Controller, MAX_CONTROLLERS> controllers_{};
    inline static uint32_t generations_{};
    inline static WaitQueue waiting_{};
    inline static WaitQueue pending_waiting_{};
    inline static bool thread_started_{};
};

}  // namespace bluetooth
