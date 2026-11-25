#include "drivers/bluetooth/hci.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <kernel/log.h>

namespace bluetooth {
namespace {

// Opcodes: command group in the top six bits, command in the rest.
constexpr uint16_t OP_INQUIRY = 0x0401;
constexpr uint16_t OP_REMOTE_NAME_REQUEST = 0x0419;
constexpr uint16_t OP_SET_EVENT_MASK = 0x0C01;
constexpr uint16_t OP_RESET = 0x0C03;
constexpr uint16_t OP_READ_LOCAL_NAME = 0x0C14;
constexpr uint16_t OP_WRITE_INQUIRY_MODE = 0x0C45;
constexpr uint16_t OP_READ_LOCAL_VERSION = 0x1001;
constexpr uint16_t OP_READ_BD_ADDR = 0x1009;
constexpr uint16_t OP_LE_SET_SCAN_PARAMETERS = 0x200B;
constexpr uint16_t OP_LE_SET_SCAN_ENABLE = 0x200C;

constexpr uint8_t EVENT_INQUIRY_COMPLETE = 0x01;
constexpr uint8_t EVENT_INQUIRY_RESULT = 0x02;
constexpr uint8_t EVENT_REMOTE_NAME_COMPLETE = 0x07;
constexpr uint8_t EVENT_COMMAND_COMPLETE = 0x0E;
constexpr uint8_t EVENT_COMMAND_STATUS = 0x0F;
constexpr uint8_t EVENT_INQUIRY_RESULT_RSSI = 0x22;
constexpr uint8_t EVENT_EXTENDED_INQUIRY_RESULT = 0x2F;
constexpr uint8_t EVENT_LE_META = 0x3E;
constexpr uint8_t LE_ADVERTISING_REPORT = 0x02;
constexpr uint8_t STATUS_SUCCESS = 0;

// Events enabled for scanning: the controller's default set (bits 0-44),
// Extended Inquiry Result (bit 46) and LE Meta (bit 61), least significant
// byte first.
constexpr uint8_t SCAN_EVENT_MASK[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x5F, 0x00, 0x20};
constexpr uint8_t INQUIRY_MODE_EXTENDED = 2;
// The general inquiry access code, then 8 x 1.28 s and no response limit.
constexpr uint8_t INQUIRY_PARAMETERS[] = {0x33, 0x8B, 0x9E, 8, 0};
constexpr uint64_t SCAN_MS = 10240;
// Active LE scan, 30 ms interval and window, public address, no filter.
constexpr uint8_t LE_SCAN_PARAMETERS[] = {1, 0x30, 0x00, 0x30, 0x00, 0, 0};
constexpr uint8_t LE_SCAN_ON[] = {1, 1};  // Enabled, duplicates filtered
constexpr uint8_t LE_SCAN_OFF[] = {0, 0};
constexpr uint16_t CLOCK_OFFSET_VALID = 0x8000;
constexpr size_t MAX_NAME_REQUESTS = 8;

constexpr size_t COMMAND_HEADER = 3;  // Opcode, parameter length
constexpr size_t EVENT_HEADER = 2;    // Code, parameter length
constexpr uint64_t COMMAND_TIMEOUT_MS = 2000;
constexpr uint64_t INQUIRY_TIMEOUT_MS = SCAN_MS + 5000;
constexpr uint64_t NAME_TIMEOUT_MS = 6000;
constexpr size_t LOCAL_NAME_BYTES = 248;

// Extended inquiry response and advertising data fields.
constexpr uint8_t AD_SHORT_NAME = 0x08;
constexpr uint8_t AD_COMPLETE_NAME = 0x09;
constexpr uint8_t AD_APPEARANCE = 0x19;

// Class of device: peripheral major class, and its keyboard/pointer bits.
constexpr uint32_t COD_MAJOR_SHIFT = 8;
constexpr uint32_t COD_MAJOR_MASK = 0x1F;
constexpr uint32_t COD_MINOR_INPUT_SHIFT = 6;
constexpr uint32_t COD_MAJOR_COMPUTER = 1;
constexpr uint32_t COD_MAJOR_PHONE = 2;
constexpr uint32_t COD_MAJOR_AUDIO = 4;
constexpr uint32_t COD_MAJOR_PERIPHERAL = 5;
constexpr uint16_t APPEARANCE_HID = 0x03C0;
constexpr uint16_t APPEARANCE_CATEGORY_MASK = 0xFFC0;
constexpr uint16_t APPEARANCE_KEYBOARD = 0x03C1;
constexpr uint16_t APPEARANCE_MOUSE = 0x03C2;

// HCI version numbers in the core specification's order.
constexpr const char* VERSION_NAMES[] = {"1.0b", "1.1", "1.2", "2.0", "2.1", "3.0", "4.0", "4.1",
                                         "4.2",  "5.0", "5.1", "5.2", "5.3", "5.4", "6.0", "6.1"};

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

struct AddressText {
    char text[3 * ADDRESS_BYTES];
};

AddressText address_text(const uint8_t* address) {
    constexpr char digits[] = "0123456789ABCDEF";
    AddressText out{};
    size_t used = 0;
    for (size_t i = ADDRESS_BYTES; i-- > 0;) {
        out.text[used++] = digits[address[i] >> 4];
        out.text[used++] = digits[address[i] & 0xF];
        out.text[used++] = i == 0 ? '\0' : ':';
    }
    return out;
}

// The name and appearance fields of extended inquiry response or
// advertising data: length-prefixed type/value records.
struct DataFields {
    const uint8_t* name{};
    size_t name_length{};
    bool complete_name{};
    uint16_t appearance{};
};

DataFields parse_fields(const uint8_t* data, size_t length) {
    DataFields fields{};
    for (size_t at = 0; at < length;) {
        const uint8_t size = data[at];
        if (size == 0 || at + 1 + size > length)
            break;
        const uint8_t type = data[at + 1];
        const uint8_t* value = data + at + 2;
        const size_t value_length = size - 1U;
        if (type == AD_COMPLETE_NAME || (type == AD_SHORT_NAME && !fields.complete_name)) {
            fields.name = value;
            fields.name_length = value_length;
            fields.complete_name = type == AD_COMPLETE_NAME;
        } else if (type == AD_APPEARANCE && value_length >= 2) {
            fields.appearance = read16(value);
        }
        at += 1U + size;
    }
    return fields;
}

// Records a name; true when it differs from the one already known.
bool set_name(Peer& peer, const uint8_t* name, size_t length) {
    char text[PEER_NAME_CAPACITY]{};
    size_t used = 0;
    while (used < length && used + 1 < PEER_NAME_CAPACITY && name[used] != 0) {
        text[used] = static_cast<char>(name[used]);
        ++used;
    }
    if (used == 0 || memcmp(text, peer.name, PEER_NAME_CAPACITY) == 0)
        return false;
    memcpy(peer.name, text, PEER_NAME_CAPACITY);
    return true;
}

const char* peer_kind(const Peer& peer) {
    if (peer.low_energy) {
        if (peer.appearance == APPEARANCE_KEYBOARD)
            return "keyboard";
        if (peer.appearance == APPEARANCE_MOUSE)
            return "mouse";
        return (peer.appearance & APPEARANCE_CATEGORY_MASK) == APPEARANCE_HID ? "input device" : nullptr;
    }
    switch ((peer.device_class >> COD_MAJOR_SHIFT) & COD_MAJOR_MASK) {
        case COD_MAJOR_COMPUTER: return "computer";
        case COD_MAJOR_PHONE: return "phone";
        case COD_MAJOR_AUDIO: return "audio";
        case COD_MAJOR_PERIPHERAL:
            switch ((peer.device_class >> COD_MINOR_INPUT_SHIFT) & 3) {
                case 1: return "keyboard";
                case 2: return "mouse";
                case 3: return "keyboard and mouse";
                default: return "input device";
            }
        default: return nullptr;
    }
}

}  // namespace

Controller* Hci::find(Transport& transport) {
    for (Controller& controller : controllers_)
        if (controller.transport == &transport)
            return &controller;
    return nullptr;
}

Error Hci::command(Controller& controller, uint16_t opcode, const uint8_t* parameters, uint8_t length, Reply& reply) {
    uint8_t packet[COMMAND_HEADER + MAX_PARAMETERS];
    packet[0] = static_cast<uint8_t>(opcode);
    packet[1] = static_cast<uint8_t>(opcode >> 8);
    packet[2] = length;
    if (length != 0)
        memcpy(packet + COMMAND_HEADER, parameters, length);
    Transport* transport = nullptr;
    uint32_t generation = 0;
    {
        intr::Guard guard;
        transport = controller.transport;
        generation = controller.generation;
        ENSURE(transport, Error::NoDevice);
        controller.awaited_opcode = opcode;
        controller.replied = false;
    }
    Error error = transport->send_command(packet, COMMAND_HEADER + length);
    intr::Guard guard;
    const uint64_t deadline = trap::uptime_ms() + COMMAND_TIMEOUT_MS;
    while (error == Error::None && !controller.replied) {
        if (controller.generation != generation)
            error = Error::NoDevice;  // Unplugged while waiting
        else if (trap::uptime_ms() >= deadline)
            error = Error::Timeout;
        else
            waiting_.sleep();
    }
    if (controller.generation != generation)
        return Error::NoDevice;
    controller.awaited_opcode = 0;
    TRY_LOG(error, "bluetooth hci%u: command 0x%04x got no reply", controller.index, opcode);
    reply.length = controller.reply_length;
    memcpy(reply.data, controller.reply, controller.reply_length);
    ENSURE_LOG(reply.length != 0 && reply.data[0] == STATUS_SUCCESS, Error::Io,
               "bluetooth hci%u: command 0x%04x failed (status 0x%02x)", controller.index, opcode,
               reply.length != 0 ? reply.data[0] : 0);
    return Error::None;
}

// Arms the wait for an event a command will cause, before the command is
// sent so that a quick event is not missed; 0 disarms it.
void Hci::expect_event(Controller& controller, uint8_t code) {
    intr::Guard guard;
    controller.awaited_event = code;
    controller.event_seen = false;
}

// Waits for the event expect_event armed. With nothing armed, this only
// passes the time, still ending early if the controller leaves.
Error Hci::wait_event(Controller& controller, uint64_t timeout_ms, Reply& event) {
    intr::Guard guard;
    const uint32_t generation = controller.generation;
    const uint64_t deadline = trap::uptime_ms() + timeout_ms;
    while (!controller.event_seen) {
        if (controller.generation != generation || !controller.transport)
            return Error::NoDevice;
        if (trap::uptime_ms() >= deadline) {
            controller.awaited_event = 0;
            return Error::Timeout;
        }
        waiting_.sleep();
    }
    event.length = controller.event_length;
    memcpy(event.data, controller.event, controller.event_length);
    return Error::None;
}

Error Hci::identify(Controller& controller) {
    Reply reply{};
    TRY(command(controller, OP_RESET, nullptr, 0, reply));

    TRY(command(controller, OP_READ_LOCAL_VERSION, nullptr, 0, reply));
    ENSURE(reply.length >= 9, Error::Io);
    const uint8_t version = reply.data[1];
    const uint16_t manufacturer = read16(reply.data + 5);
    const uint16_t subversion = read16(reply.data + 7);

    TRY(command(controller, OP_READ_BD_ADDR, nullptr, 0, reply));
    ENSURE(reply.length >= 1 + ADDRESS_BYTES, Error::Io);
    const AddressText address = address_text(reply.data + 1);

    if (version < sizeof(VERSION_NAMES) / sizeof(VERSION_NAMES[0]))
        klog::info("bluetooth hci%u: %s, Bluetooth %s, manufacturer 0x%04x, subversion 0x%04x\n", controller.index,
                   address.text, VERSION_NAMES[version], manufacturer, subversion);
    else
        klog::info("bluetooth hci%u: %s, HCI version 0x%02x, manufacturer 0x%04x, subversion 0x%04x\n",
                   controller.index, address.text, version, manufacturer, subversion);

    // The name is optional information; a controller that refuses it still works.
    if (command(controller, OP_READ_LOCAL_NAME, nullptr, 0, reply) == Error::None &&
        reply.length >= 1 + LOCAL_NAME_BYTES) {
        static_assert(1 + LOCAL_NAME_BYTES < MAX_PARAMETERS);
        reply.data[1 + LOCAL_NAME_BYTES] = 0;  // Names shorter than the field end in a zero
        const auto* name = reinterpret_cast<const char*>(reply.data + 1);
        if (name[0] != '\0')
            klog::info("bluetooth hci%u: name \"%s\"\n", controller.index, name);
    }
    return Error::None;
}

// A classic inquiry and an LE scan side by side; devices are logged as their
// results arrive, then classic devices that gave no name are asked for it.
Error Hci::scan(Controller& controller) {
    uint32_t generation = 0;
    {
        intr::Guard guard;
        generation = controller.generation;
    }
    Reply reply{};
    TRY(command(controller, OP_SET_EVENT_MASK, SCAN_EVENT_MASK, sizeof(SCAN_EVENT_MASK), reply));
    // Results then carry the name most devices put in their response.
    static_cast<void>(command(controller, OP_WRITE_INQUIRY_MODE, &INQUIRY_MODE_EXTENDED, 1, reply));
    bool low_energy = command(controller, OP_LE_SET_SCAN_PARAMETERS, LE_SCAN_PARAMETERS, sizeof(LE_SCAN_PARAMETERS),
                              reply) == Error::None;
    if (low_energy)
        low_energy = command(controller, OP_LE_SET_SCAN_ENABLE, LE_SCAN_ON, sizeof(LE_SCAN_ON), reply) == Error::None;
    klog::info("bluetooth hci%u: scanning for %u seconds%s\n", controller.index,
               static_cast<unsigned int>(SCAN_MS / 1000), low_energy ? "" : " (classic only)");

    expect_event(controller, EVENT_INQUIRY_COMPLETE);
    Error error = command(controller, OP_INQUIRY, INQUIRY_PARAMETERS, sizeof(INQUIRY_PARAMETERS), reply);
    if (error == Error::None) {
        error = wait_event(controller, INQUIRY_TIMEOUT_MS, reply);
    } else if (error != Error::NoDevice) {
        expect_event(controller, 0);
        error = low_energy ? wait_event(controller, SCAN_MS, reply) : error;  // LE results still arrive
        if (error == Error::Timeout)
            error = Error::None;
    }
    if (error == Error::NoDevice)
        return error;
    if (low_energy)
        static_cast<void>(command(controller, OP_LE_SET_SCAN_ENABLE, LE_SCAN_OFF, sizeof(LE_SCAN_OFF), reply));
    request_names(controller, generation);
    intr::Guard guard;
    if (controller.generation == generation)
        klog::info("bluetooth hci%u: scan finished, %u devices\n", controller.index,
                   static_cast<unsigned int>(controller.peer_count));
    return Error::None;
}

void Hci::request_names(Controller& controller, uint32_t generation) {
    size_t requests = 0;
    for (size_t i = 0; requests < MAX_NAME_REQUESTS; ++i) {
        uint8_t parameters[ADDRESS_BYTES + 4];
        {
            intr::Guard guard;
            if (controller.generation != generation || i >= controller.peer_count)
                return;
            const Peer& peer = controller.peers[i];
            if (peer.low_energy || peer.name[0] != '\0')
                continue;
            memcpy(parameters, peer.address, ADDRESS_BYTES);
            parameters[ADDRESS_BYTES] = peer.page_scan_mode;
            parameters[ADDRESS_BYTES + 1] = 0;
            const uint16_t clock = peer.clock_offset | CLOCK_OFFSET_VALID;
            parameters[ADDRESS_BYTES + 2] = static_cast<uint8_t>(clock);
            parameters[ADDRESS_BYTES + 3] = static_cast<uint8_t>(clock >> 8);
        }
        ++requests;
        Reply event{};
        expect_event(controller, EVENT_REMOTE_NAME_COMPLETE);
        Error error = command(controller, OP_REMOTE_NAME_REQUEST, parameters, sizeof(parameters), event);
        if (error == Error::None)
            error = wait_event(controller, NAME_TIMEOUT_MS, event);
        else
            expect_event(controller, 0);
        if (error == Error::NoDevice)
            return;
        // Status, address, then the name.
        if (error != Error::None || event.length < 1 + ADDRESS_BYTES || event.data[0] != STATUS_SUCCESS ||
            memcmp(event.data + 1, parameters, ADDRESS_BYTES) != 0)
            continue;
        intr::Guard guard;
        if (controller.generation != generation)
            return;
        Peer& peer = controller.peers[i];
        if (set_name(peer, event.data + 1 + ADDRESS_BYTES, event.length - 1 - ADDRESS_BYTES))
            log_peer(controller, peer);
    }
}

Error Hci::attach(Transport& transport) {
    Controller* controller = nullptr;
    {
        intr::Guard guard;
        for (size_t i = 0; i < controllers_.size(); ++i) {
            if (controllers_[i].transport)
                continue;
            controllers_[i] = Controller{};
            controllers_[i].transport = &transport;
            controllers_[i].index = static_cast<uint8_t>(i);
            controllers_[i].generation = ++generations_;
            controller = &controllers_[i];
            break;
        }
    }
    ENSURE_LOG(controller, Error::Full, "bluetooth: too many controllers");
    // Bringing a controller up takes seconds and deep calls, so it runs in
    // the Bluetooth thread instead of holding up the caller or its stack.
    if (!thread_started_) {
        auto pid = sched::kernel_thread(thread, nullptr);
        if (!pid.ok()) {
            detach(transport);
            klog::warn("bluetooth: cannot start the Bluetooth thread\n");
            return pid.error();
        }
        thread_started_ = true;
    }
    intr::Guard guard;
    controller->bring_up_pending = true;
    pending_waiting_.wakeup_all();
    return Error::None;
}

void Hci::detach(Transport& transport) {
    intr::Guard guard;
    for (Controller& controller : controllers_)
        if (controller.transport == &transport)
            controller = Controller{};
    waiting_.wakeup_all();  // Waits on its commands end now
}

Peer* Hci::peer(Controller& controller, const uint8_t* address, bool low_energy) {
    for (size_t i = 0; i < controller.peer_count; ++i) {
        Peer& known = controller.peers[i];
        if (known.low_energy == low_energy && memcmp(known.address, address, ADDRESS_BYTES) == 0)
            return &known;
    }
    if (controller.peer_count == controller.peers.size())
        return nullptr;
    Peer& added = controller.peers[controller.peer_count++];
    added = Peer{};
    memcpy(added.address, address, ADDRESS_BYTES);
    added.low_energy = low_energy;
    return &added;
}

void Hci::log_peer(const Controller& controller, const Peer& peer) {
    const AddressText address = address_text(peer.address);
    const char* kind = peer_kind(peer);
    klog::info("bluetooth hci%u: %s %s%s%s%s%s%s\n", controller.index, address.text, peer.low_energy ? "LE" : "classic",
               kind ? ", " : "", kind ? kind : "", peer.name[0] ? " \"" : "", peer.name, peer.name[0] ? "\"" : "");
}

void Hci::inquiry_result(Controller& controller, const uint8_t* address, uint8_t page_scan_mode,
                         const uint8_t* device_class, const uint8_t* clock_offset, const uint8_t* eir,
                         size_t eir_length) {
    const size_t known = controller.peer_count;
    Peer* found = peer(controller, address, false);
    if (!found)
        return;
    const bool added = controller.peer_count != known;
    found->page_scan_mode = page_scan_mode;
    found->device_class = device_class[0] | (device_class[1] << 8) | (static_cast<uint32_t>(device_class[2]) << 16);
    found->clock_offset = read16(clock_offset);
    bool named = false;
    if (eir) {
        const DataFields fields = parse_fields(eir, eir_length);
        if (fields.name)
            named = set_name(*found, fields.name, fields.name_length);
    }
    if (added || named)
        log_peer(controller, *found);
}

// LE Advertising Report: a count, then each report's event type, address
// type, address, data length, data and RSSI.
void Hci::advertising_reports(Controller& controller, const uint8_t* parameters, size_t length) {
    if (length < 1)
        return;
    size_t at = 1;
    for (uint8_t report = 0; report < parameters[0]; ++report) {
        if (at + 2 + ADDRESS_BYTES + 1 > length)
            return;
        const uint8_t address_type = parameters[at + 1];
        const uint8_t* address = parameters + at + 2;
        const uint8_t data_length = parameters[at + 2 + ADDRESS_BYTES];
        const uint8_t* data = parameters + at + 3 + ADDRESS_BYTES;
        if (at + 3 + ADDRESS_BYTES + data_length + 1 > length)
            return;
        at += 3 + ADDRESS_BYTES + data_length + 1;

        const size_t known = controller.peer_count;
        Peer* found = peer(controller, address, true);
        if (!found)
            continue;
        bool changed = controller.peer_count != known;
        found->address_type = address_type;
        const DataFields fields = parse_fields(data, data_length);
        if (fields.appearance != 0 && fields.appearance != found->appearance) {
            found->appearance = fields.appearance;
            changed = true;
        }
        if (fields.name && set_name(*found, fields.name, fields.name_length))
            changed = true;
        if (changed)
            log_peer(controller, *found);
    }
}

void Hci::event_received(Transport& transport, const uint8_t* event, size_t length) {
    if (length < EVENT_HEADER || length < EVENT_HEADER + event[1])
        return;
    intr::Guard guard;
    Controller* controller = find(transport);
    if (!controller)
        return;
    const uint8_t code = event[0];
    const uint8_t* parameters = event + EVENT_HEADER;
    const uint8_t size = event[1];
    // Command Complete: credits, opcode, return parameters (status first).
    // Command Status: status, credits, opcode.
    if (code == EVENT_COMMAND_COMPLETE && size >= 3 && controller->awaited_opcode != 0 &&
        read16(parameters + 1) == controller->awaited_opcode) {
        controller->reply_length = static_cast<uint8_t>(size - 3);
        memcpy(controller->reply, parameters + 3, controller->reply_length);
        controller->replied = true;
        waiting_.wakeup_all();
        return;
    }
    if (code == EVENT_COMMAND_STATUS && size >= 4 && controller->awaited_opcode != 0 &&
        read16(parameters + 2) == controller->awaited_opcode) {
        controller->reply_length = 1;
        controller->reply[0] = parameters[0];
        controller->replied = true;
        waiting_.wakeup_all();
        return;
    }
    if (controller->awaited_event != 0 && code == controller->awaited_event) {
        controller->event_length = size;
        memcpy(controller->event, parameters, size);
        controller->event_seen = true;
        controller->awaited_event = 0;
        waiting_.wakeup_all();
        return;
    }
    // Inquiry results list each field for all responses in turn.
    const size_t responses = size != 0 ? parameters[0] : 0;
    switch (code) {
        case EVENT_INQUIRY_RESULT:
            if (size < 1 + 14 * responses)
                return;
            for (size_t i = 0; i < responses; ++i)
                inquiry_result(*controller, parameters + 1 + 6 * i, parameters[1 + 6 * responses + i],
                               parameters + 1 + 9 * responses + 3 * i, parameters + 1 + 12 * responses + 2 * i, nullptr,
                               0);
            return;
        case EVENT_INQUIRY_RESULT_RSSI:
            if (size < 1 + 14 * responses)
                return;
            for (size_t i = 0; i < responses; ++i)
                inquiry_result(*controller, parameters + 1 + 6 * i, parameters[1 + 6 * responses + i],
                               parameters + 1 + 8 * responses + 3 * i, parameters + 1 + 11 * responses + 2 * i, nullptr,
                               0);
            return;
        case EVENT_EXTENDED_INQUIRY_RESULT:
            // One response: address, scan mode, reserved, class, clock, RSSI, data.
            if (size >= 15)
                inquiry_result(*controller, parameters + 1, parameters[7], parameters + 9, parameters + 12,
                               parameters + 15, size - 15U);
            return;
        case EVENT_LE_META:
            if (size >= 1 && parameters[0] == LE_ADVERTISING_REPORT)
                advertising_reports(*controller, parameters + 1, size - 1U);
            return;
        default: klog::debug("bluetooth hci%u: event 0x%02x\n", controller->index, code); return;
    }
}

void Hci::bring_up(Controller& controller) {
    const Error error = identify(controller);
    if (error == Error::None)
        static_cast<void>(scan(controller));
    else if (error != Error::NoDevice)
        klog::warn("bluetooth hci%u: controller not usable (%d)\n", controller.index, static_cast<int>(error));
}

int Hci::thread(void*) {
    for (;;)
        bring_up(*next_pending());
}

Controller* Hci::next_pending() {
    intr::Guard guard;
    for (;;) {
        for (Controller& controller : controllers_) {
            if (controller.transport && controller.bring_up_pending) {
                controller.bring_up_pending = false;
                return &controller;
            }
        }
        pending_waiting_.sleep();
    }
}

void Hci::tick() {
    if (!waiting_.empty())
        waiting_.wakeup_all();
}

}  // namespace bluetooth
