#pragma once

// Bluetooth host controller interface (HCI): the commands and events every
// controller speaks, whatever carries them. A transport driver (USB today)
// moves packets; this layer brings each controller up and reports what it is.

#include <base/types.h>
#include "lib/result.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>

namespace bluetooth {

// Carries HCI packets to and from one controller.
class Transport {
public:
    // Sends a command packet: opcode, parameter length, parameters.
    virtual Error send_command(const uint8_t* packet, size_t length) = 0;

protected:
    ~Transport() = default;
};

inline constexpr size_t HCI_MAX_PARAMETERS = 255;

// One controller the HCI layer has brought up.
struct Controller {
    Transport* transport{};  // Null while the entry is free
    uint8_t index{};
    uint16_t awaited_opcode{};
    bool replied{};
    uint8_t reply_length{};
    uint8_t reply[HCI_MAX_PARAMETERS]{};  // Return parameters, status first
};

// A command's return parameters, status first.
struct Reply {
    uint8_t length{};
    uint8_t data[HCI_MAX_PARAMETERS]{};
};

class Hci {
public:
    static constexpr size_t MAX_CONTROLLERS = 2;
    static constexpr size_t MAX_PARAMETERS = HCI_MAX_PARAMETERS;

    // Brings up a controller that has just appeared and logs what it is.
    // Runs in a kernel thread that may sleep; events must already flow.
    static Error attach(Transport& transport);
    // Forgets a controller whose transport is leaving.
    static void detach(Transport& transport);
    // One complete event from a controller: code, length, parameters. Any context.
    static void event_received(Transport& transport, const uint8_t* event, size_t length);
    // Timer tick: lets command waits notice their timeout.
    static void tick();

private:
    static Error command(Controller& controller, uint16_t opcode, const uint8_t* parameters, uint8_t length,
                         Reply& reply);
    static Error identify(Controller& controller);

    inline static sys::array<Controller, MAX_CONTROLLERS> controllers_{};
    inline static WaitQueue waiting_{};
};

}  // namespace bluetooth
