#include "drivers/bluetooth/hci.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include "trap/trap.h"
#include <kernel/log.h>

namespace bluetooth {
namespace {

// Opcodes: command group in the top six bits, command in the rest.
constexpr uint16_t OP_RESET = 0x0C03;
constexpr uint16_t OP_READ_LOCAL_NAME = 0x0C14;
constexpr uint16_t OP_READ_LOCAL_VERSION = 0x1001;
constexpr uint16_t OP_READ_BD_ADDR = 0x1009;

constexpr uint8_t EVENT_COMMAND_COMPLETE = 0x0E;
constexpr uint8_t EVENT_COMMAND_STATUS = 0x0F;
constexpr uint8_t STATUS_SUCCESS = 0;

constexpr size_t COMMAND_HEADER = 3;  // Opcode, parameter length
constexpr size_t EVENT_HEADER = 2;    // Code, parameter length
constexpr uint64_t COMMAND_TIMEOUT_MS = 2000;

constexpr size_t BD_ADDR_BYTES = 6;
constexpr size_t LOCAL_NAME_BYTES = 248;

// HCI version numbers in the core specification's order.
constexpr const char* VERSION_NAMES[] = {"1.0b", "1.1", "1.2", "2.0", "2.1", "3.0", "4.0", "4.1",
                                         "4.2",  "5.0", "5.1", "5.2", "5.3", "5.4", "6.0", "6.1"};

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

}  // namespace

Error Hci::command(Controller& controller, uint16_t opcode, const uint8_t* parameters, uint8_t length, Reply& reply) {
    uint8_t packet[COMMAND_HEADER + MAX_PARAMETERS];
    packet[0] = static_cast<uint8_t>(opcode);
    packet[1] = static_cast<uint8_t>(opcode >> 8);
    packet[2] = length;
    if (length != 0)
        memcpy(packet + COMMAND_HEADER, parameters, length);
    {
        intr::Guard guard;
        controller.awaited_opcode = opcode;
        controller.replied = false;
    }
    Error error = controller.transport->send_command(packet, COMMAND_HEADER + length);
    intr::Guard guard;
    const uint64_t deadline = trap::uptime_ms() + COMMAND_TIMEOUT_MS;
    while (error == Error::None && !controller.replied) {
        if (trap::uptime_ms() >= deadline)
            error = Error::Timeout;
        else
            waiting_.sleep();
    }
    controller.awaited_opcode = 0;
    TRY_LOG(error, "bluetooth hci%u: command 0x%04x got no reply", controller.index, opcode);
    reply.length = controller.reply_length;
    memcpy(reply.data, controller.reply, controller.reply_length);
    ENSURE_LOG(reply.length != 0 && reply.data[0] == STATUS_SUCCESS, Error::Io,
               "bluetooth hci%u: command 0x%04x failed (status 0x%02x)", controller.index, opcode,
               reply.length != 0 ? reply.data[0] : 0);
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
    ENSURE(reply.length >= 1 + BD_ADDR_BYTES, Error::Io);
    const uint8_t* address = reply.data + 1;  // Least significant byte first

    if (version < sizeof(VERSION_NAMES) / sizeof(VERSION_NAMES[0]))
        klog::info("bluetooth hci%u: %02x:%02x:%02x:%02x:%02x:%02x, Bluetooth %s, manufacturer 0x%04x, "
                   "subversion 0x%04x\n",
                   controller.index, address[5], address[4], address[3], address[2], address[1], address[0],
                   VERSION_NAMES[version], manufacturer, subversion);
    else
        klog::info("bluetooth hci%u: %02x:%02x:%02x:%02x:%02x:%02x, HCI version 0x%02x, manufacturer 0x%04x, "
                   "subversion 0x%04x\n",
                   controller.index, address[5], address[4], address[3], address[2], address[1], address[0], version,
                   manufacturer, subversion);

    // The name is optional information; a controller that refuses it still works.
    if (command(controller, OP_READ_LOCAL_NAME, nullptr, 0, reply) == Error::None &&
        reply.length >= 1 + LOCAL_NAME_BYTES) {
        char name[LOCAL_NAME_BYTES + 1];
        memcpy(name, reply.data + 1, LOCAL_NAME_BYTES);
        name[LOCAL_NAME_BYTES] = '\0';
        if (name[0] != '\0')
            klog::info("bluetooth hci%u: name \"%s\"\n", controller.index, name);
    }
    return Error::None;
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
            controller = &controllers_[i];
            break;
        }
    }
    ENSURE_LOG(controller, Error::Full, "bluetooth: too many controllers");
    const Error error = identify(*controller);
    if (error != Error::None) {
        klog::warn("bluetooth hci%u: controller not usable (%d)\n", controller->index, static_cast<int>(error));
        detach(transport);
    }
    return error;
}

void Hci::detach(Transport& transport) {
    intr::Guard guard;
    for (Controller& controller : controllers_)
        if (controller.transport == &transport)
            controller = Controller{};
}

void Hci::event_received(Transport& transport, const uint8_t* event, size_t length) {
    if (length < EVENT_HEADER || length < EVENT_HEADER + event[1])
        return;
    intr::Guard guard;
    Controller* controller = nullptr;
    for (Controller& candidate : controllers_)
        if (candidate.transport == &transport)
            controller = &candidate;
    if (!controller)
        return;
    const uint8_t code = event[0];
    const uint8_t* parameters = event + EVENT_HEADER;
    const uint8_t size = event[1];
    // Command Complete: credits, opcode, return parameters (status first).
    // Command Status: status, credits, opcode.
    if (code == EVENT_COMMAND_COMPLETE && size >= 3 && read16(parameters + 1) == controller->awaited_opcode &&
        controller->awaited_opcode != 0) {
        controller->reply_length = static_cast<uint8_t>(size - 3);
        memcpy(controller->reply, parameters + 3, controller->reply_length);
    } else if (code == EVENT_COMMAND_STATUS && size >= 4 && read16(parameters + 2) == controller->awaited_opcode &&
               controller->awaited_opcode != 0) {
        controller->reply_length = 1;
        controller->reply[0] = parameters[0];
    } else {
        klog::debug("bluetooth hci%u: event 0x%02x\n", controller->index, code);
        return;
    }
    controller->replied = true;
    waiting_.wakeup_all();
}

void Hci::tick() {
    if (!waiting_.empty())
        waiting_.wakeup_all();
}

}  // namespace bluetooth
