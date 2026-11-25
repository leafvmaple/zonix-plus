// The LE connection: creating it, ACL data with the controller's flow
// control, L2CAP frames on the fixed channels, and the requests a keyboard
// sends its host (ATT requests and connection parameter updates).
#include "drivers/bluetooth/hci.h"
#include "drivers/bluetooth/hci_defs.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include "trap/trap.h"
#include <kernel/log.h>

namespace bluetooth {
namespace {

// LE Create Connection: 60 ms scan interval, 30 ms window, no filter list,
// then (after the peer) our public address, a 30-50 ms connection interval,
// no latency, a 5 s supervision timeout and no event length hint.
constexpr uint8_t CONNECT_SCAN[] = {0x60, 0x00, 0x30, 0x00, 0};
constexpr uint8_t CONNECT_TIMING[] = {0, 0x18, 0x00, 0x28, 0x00, 0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00};
// Passive scan while waiting for the bonded keyboard: 60 ms interval, 30 ms window.
constexpr uint8_t WATCH_SCAN_PARAMETERS[] = {0, 0x60, 0x00, 0x30, 0x00, 0, 0};
constexpr uint64_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint64_t CANCEL_TIMEOUT_MS = 2000;
constexpr uint64_t CREDIT_TIMEOUT_MS = 2000;
constexpr uint64_t SERVE_POLL_MS = 1000;

// L2CAP signaling.
constexpr uint8_t SIGNAL_COMMAND_REJECT = 0x01;
constexpr uint8_t SIGNAL_PARAMETER_UPDATE_REQUEST = 0x12;
constexpr uint8_t SIGNAL_PARAMETER_UPDATE_RESPONSE = 0x13;
constexpr uint8_t SIGNAL_CREDIT_INDICATION = 0x16;
constexpr uint8_t SIGNAL_CREDIT_CONNECTION_RESPONSE = 0x18;
constexpr uint8_t SIGNAL_RECONFIGURE_RESPONSE = 0x1A;

// ATT requests a client sends; the host answers them as a server with
// nothing to offer.
bool client_request(uint8_t opcode) {
    return (opcode & 1) == 0 && opcode >= ATT_MTU_REQUEST && opcode < ATT_NOTIFICATION;
}

}  // namespace

Error Hci::connect(Controller& controller, const uint8_t* address, uint8_t address_type) {
    uint8_t parameters[sizeof(CONNECT_SCAN) + 1 + ADDRESS_BYTES + sizeof(CONNECT_TIMING)];
    memcpy(parameters, CONNECT_SCAN, sizeof(CONNECT_SCAN));
    parameters[sizeof(CONNECT_SCAN)] = address_type;
    memcpy(parameters + sizeof(CONNECT_SCAN) + 1, address, ADDRESS_BYTES);
    memcpy(parameters + sizeof(CONNECT_SCAN) + 1 + ADDRESS_BYTES, CONNECT_TIMING, sizeof(CONNECT_TIMING));
    Reply reply{};
    expect_event(controller, EVENT_LE_META, LE_CONNECTION_COMPLETE);
    Error error = command(controller, OP_LE_CREATE_CONNECTION, parameters, sizeof(parameters), reply);
    if (error != Error::None) {
        expect_event(controller, 0);
        return error;
    }
    Reply event{};
    error = wait_event(controller, CONNECT_TIMEOUT_MS, event);
    if (error == Error::Timeout) {
        // Cancelling still ends in a Connection Complete, with an error status.
        expect_event(controller, EVENT_LE_META, LE_CONNECTION_COMPLETE);
        if (command(controller, OP_LE_CREATE_CONNECTION_CANCEL, nullptr, 0, reply) == Error::None)
            static_cast<void>(wait_event(controller, CANCEL_TIMEOUT_MS, event));
        klog::warn("bluetooth hci%u: %s did not answer\n", controller.index, address_text(address).text);
        return Error::Timeout;
    }
    TRY(error);
    // Subevent, status, handle, role, peer address type and address, timing.
    ENSURE(event.length >= 4, Error::Io);
    ENSURE_LOG(event.data[1] == STATUS_SUCCESS, Error::Io, "bluetooth hci%u: connection failed (status 0x%02x)",
               controller.index, event.data[1]);
    intr::Guard guard;
    controller.link = Link{};
    controller.link.connected = true;
    controller.link.handle = read16(event.data + 2) & ACL_HANDLE_MASK;
    controller.acl_credits = controller.acl_buffers;
    klog::info("bluetooth hci%u: connected to %s\n", controller.index, address_text(address).text);
    return Error::None;
}

// Scans until the bonded keyboard advertises.
Error Hci::wait_for_bond(Controller& controller) {
    Reply reply{};
    TRY(command(controller, OP_LE_SET_SCAN_PARAMETERS, WATCH_SCAN_PARAMETERS, sizeof(WATCH_SCAN_PARAMETERS), reply));
    {
        intr::Guard guard;
        controller.watching_for_bond = true;
        controller.bond_seen = false;
    }
    Error error = command(controller, OP_LE_SET_SCAN_ENABLE, LE_SCAN_ON, sizeof(LE_SCAN_ON), reply);
    if (error == Error::None) {
        intr::Guard guard;
        const uint32_t generation = controller.generation;
        while (!controller.bond_seen && error == Error::None) {
            if (controller.generation != generation)
                return Error::NoDevice;
            waiting_.sleep();
        }
    }
    {
        intr::Guard guard;
        controller.watching_for_bond = false;
    }
    if (error == Error::None)
        error = command(controller, OP_LE_SET_SCAN_ENABLE, LE_SCAN_OFF, sizeof(LE_SCAN_OFF), reply);
    return error;
}

// Sends one L2CAP frame in one ACL packet, once the controller has a free
// buffer for it.
Error Hci::send_frame(Controller& controller, uint16_t channel, const uint8_t* payload, size_t length) {
    uint8_t packet[ACL_HEADER + L2CAP_HEADER + FRAME_CAPACITY];
    ENSURE(length <= FRAME_CAPACITY, Error::Invalid);
    Transport* transport = nullptr;
    {
        intr::Guard guard;
        ENSURE(controller.transport, Error::NoDevice);
        ENSURE(L2CAP_HEADER + length <= controller.acl_mtu, Error::Invalid);
        const uint32_t generation = controller.generation;
        const uint64_t deadline = trap::uptime_ms() + CREDIT_TIMEOUT_MS;
        while (controller.acl_credits == 0) {
            if (controller.generation != generation)
                return Error::NoDevice;
            ENSURE(controller.link.connected, Error::Interrupted);
            ENSURE_LOG(trap::uptime_ms() < deadline, Error::Timeout, "bluetooth hci%u: no free ACL buffer",
                       controller.index);
            waiting_.sleep();
        }
        ENSURE(controller.link.connected, Error::Interrupted);
        --controller.acl_credits;
        transport = controller.transport;
        write16(packet, controller.link.handle | (ACL_FIRST_NON_FLUSHABLE << ACL_BOUNDARY_SHIFT));
    }
    write16(packet + 2, static_cast<uint16_t>(L2CAP_HEADER + length));
    write16(packet + 4, static_cast<uint16_t>(length));
    write16(packet + 6, channel);
    memcpy(packet + ACL_HEADER + L2CAP_HEADER, payload, length);
    return transport->send_acl(packet, ACL_HEADER + L2CAP_HEADER + length);
}

// Waits for a frame on `channel`, answering the keyboard's own requests
// meanwhile. Channel 0 waits for nothing: it only answers requests until
// the timeout. Interrupted when the link drops.
Error Hci::wait_frame(Controller& controller, uint16_t channel, uint64_t timeout_ms, Frame& frame) {
    const uint64_t deadline = trap::uptime_ms() + timeout_ms;
    for (;;) {
        Frame next{};
        {
            intr::Guard guard;
            const uint32_t generation = controller.generation;
            while (controller.link.frame_count == 0) {
                if (controller.generation != generation)
                    return Error::NoDevice;
                if (!controller.link.connected)
                    return Error::Interrupted;
                if (trap::uptime_ms() >= deadline)
                    return Error::Timeout;
                waiting_.sleep();
            }
            Link& link = controller.link;
            next = link.frames[0];
            for (size_t i = 1; i < link.frame_count; ++i)
                link.frames[i - 1] = link.frames[i];
            --link.frame_count;
        }
        if (answer_request(controller, next))
            continue;
        if (channel != 0 && next.channel == channel) {
            frame = next;
            return Error::None;
        }
        klog::debug("bluetooth hci%u: ignored frame on channel %u (0x%02x)\n", controller.index, next.channel,
                    next.length != 0 ? next.data[0] : 0);
    }
}

// Answers what the keyboard asks of its host; false for frames that are not
// requests.
bool Hci::answer_request(Controller& controller, const Frame& frame) {
    if (frame.length == 0)
        return false;
    const uint8_t code = frame.data[0];
    if (frame.channel == CHANNEL_ATT) {
        if (code == ATT_MTU_REQUEST) {
            uint8_t response[3] = {ATT_MTU_RESPONSE};
            write16(response + 1, ATT_DEFAULT_MTU);
            static_cast<void>(send_frame(controller, CHANNEL_ATT, response, sizeof(response)));
            return true;
        }
        if (code == ATT_INDICATION) {
            const uint8_t confirmation = ATT_CONFIRMATION;
            static_cast<void>(send_frame(controller, CHANNEL_ATT, &confirmation, 1));
            return true;
        }
        if (code == ATT_WRITE_COMMAND || code == ATT_SIGNED_WRITE_COMMAND || code == ATT_CONFIRMATION)
            return true;
        if (!client_request(code))
            return false;
        // The host has no attributes of its own to offer.
        const bool discovery = code == ATT_FIND_INFORMATION_REQUEST || code == ATT_FIND_BY_TYPE_REQUEST ||
                               code == ATT_READ_BY_TYPE_REQUEST || code == ATT_READ_BY_GROUP_REQUEST;
        uint8_t error[5] = {ATT_ERROR, code, 0, 0, discovery ? ATT_ERROR_NOT_FOUND : ATT_ERROR_REQUEST_NOT_SUPPORTED};
        if (frame.length >= 3)
            write16(error + 2, read16(frame.data + 1));
        static_cast<void>(send_frame(controller, CHANNEL_ATT, error, sizeof(error)));
        return true;
    }
    if (frame.channel == CHANNEL_SIGNALING) {
        if (frame.length < 4)
            return true;
        const uint8_t identifier = frame.data[1];
        if (code == SIGNAL_PARAMETER_UPDATE_REQUEST && frame.length >= 12) {
            // Accept the keyboard's timing (interval range, latency,
            // timeout) and have the controller apply it.
            const uint8_t response[6] = {SIGNAL_PARAMETER_UPDATE_RESPONSE, identifier, 2, 0, 0, 0};
            static_cast<void>(send_frame(controller, CHANNEL_SIGNALING, response, sizeof(response)));
            uint8_t update[14]{};
            {
                intr::Guard guard;
                write16(update, controller.link.handle);
            }
            memcpy(update + 2, frame.data + 4, 8);
            Reply reply{};
            static_cast<void>(command(controller, OP_LE_CONNECTION_UPDATE, update, sizeof(update), reply));
            return true;
        }
        const bool response = (code & 1) != 0 || code == SIGNAL_CREDIT_INDICATION ||
                              code == SIGNAL_CREDIT_CONNECTION_RESPONSE || code == SIGNAL_RECONFIGURE_RESPONSE;
        if (!response) {
            const uint8_t reject[6] = {SIGNAL_COMMAND_REJECT, identifier, 2, 0, 0, 0};  // Not understood
            static_cast<void>(send_frame(controller, CHANNEL_SIGNALING, reject, sizeof(reject)));
        }
        return true;
    }
    return false;
}

// Keeps the connection served while it lasts; keyboard reports go straight
// to the console as they arrive.
Error Hci::serve(Controller& controller) {
    for (;;) {
        Frame ignored{};
        const Error error = wait_frame(controller, 0, SERVE_POLL_MS, ignored);
        if (error != Error::Timeout)
            return error;
    }
}

void Hci::acl_received(Transport& transport, const uint8_t* packet, size_t length) {
    if (length < ACL_HEADER)
        return;
    intr::Guard guard;
    Controller* controller = find(transport);
    if (!controller || !controller->link.connected)
        return;
    Link& link = controller->link;
    const uint16_t header = read16(packet);
    const size_t data_length = read16(packet + 2);
    if ((header & ACL_HANDLE_MASK) != link.handle || ACL_HEADER + data_length > length)
        return;
    const uint8_t* data = packet + ACL_HEADER;
    const uint16_t boundary = (header >> ACL_BOUNDARY_SHIFT) & ACL_BOUNDARY_MASK;
    if (boundary == ACL_FIRST) {
        link.assembled = 0;
        link.expected = data_length >= 2 ? L2CAP_HEADER + read16(data) : 0;
        if (link.expected > sizeof(link.assembly)) {
            klog::debug("bluetooth hci%u: dropped a %u-byte frame\n", controller->index,
                        static_cast<unsigned int>(link.expected));
            link.expected = 0;  // Larger than anything the host asked for
        }
    } else if (boundary != ACL_CONTINUING) {
        return;
    }
    if (link.expected == 0)
        return;
    const size_t take = data_length < link.expected - link.assembled ? data_length : link.expected - link.assembled;
    memcpy(link.assembly + link.assembled, data, take);
    link.assembled += take;
    if (link.assembled < link.expected || link.assembled < L2CAP_HEADER)
        return;
    const size_t payload = link.expected - L2CAP_HEADER;
    link.expected = 0;
    frame_received(*controller, read16(link.assembly + 2), link.assembly + L2CAP_HEADER, payload);
}

// Keyboard reports are handled at once; anything else waits for the thread.
// Runs with interrupts disabled.
void Hci::frame_received(Controller& controller, uint16_t channel, const uint8_t* payload, size_t length) {
    Link& link = controller.link;
    if (channel == CHANNEL_ATT && length >= 3 && (payload[0] == ATT_NOTIFICATION || payload[0] == ATT_INDICATION)) {
        const uint16_t handle = read16(payload + 1);
        for (size_t i = 0; i < link.report_count; ++i)
            if (link.reports[i] == handle)
                link.keyboard.report(payload + 3, length - 3);
        if (payload[0] == ATT_NOTIFICATION)
            return;  // Indications still need the thread's confirmation
    }
    if (link.frame_count == link.frames.size() || length > FRAME_CAPACITY) {
        klog::warn("bluetooth hci%u: dropped a frame on channel %u\n", controller.index, channel);
        return;
    }
    Frame& frame = link.frames[link.frame_count++];
    frame.channel = channel;
    frame.length = static_cast<uint8_t>(length);
    memcpy(frame.data, payload, length);
    waiting_.wakeup_all();
}

void Hci::link_lost(Controller& controller, uint16_t handle, uint8_t reason) {
    Link& link = controller.link;
    if (!link.connected || link.handle != handle)
        return;
    link.keyboard.release();
    link.connected = false;
    link.disconnect_reason = reason;
    link.report_count = 0;
    // The controller frees the buffers of a link that is gone.
    controller.acl_credits = controller.acl_buffers;
    klog::info("bluetooth hci%u: disconnected (reason 0x%02x)\n", controller.index, reason);
    waiting_.wakeup_all();
}

// Number Of Completed Packets: a count, then each handle and the buffers it
// gave back.
void Hci::packets_completed(Controller& controller, const uint8_t* parameters, size_t length) {
    if (length < 1)
        return;
    const size_t handles = parameters[0];
    if (length < 1 + 4 * handles)
        return;
    for (size_t i = 0; i < handles; ++i) {
        const uint32_t credits = controller.acl_credits + read16(parameters + 1 + 2 * handles + 2 * i);
        controller.acl_credits =
            static_cast<uint16_t>(credits < controller.acl_buffers ? credits : controller.acl_buffers);
    }
    waiting_.wakeup_all();
}

}  // namespace bluetooth
