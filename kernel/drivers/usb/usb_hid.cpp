#include "drivers/usb/usb_hid.h"
#include "cons/cons.h"
#include "drivers/boot_keyboard.h"
#include "drivers/intr.h"
#include "trap/trap.h"
#include <abi/display.h>
#include <kernel/log.h>
#include <sys/array.hpp>

namespace usb::hid {
namespace {

constexpr uint8_t CLASS_HID = 3;
constexpr uint8_t SUBCLASS_BOOT = 1;
constexpr uint8_t PROTOCOL_KEYBOARD = 1;
constexpr uint8_t PROTOCOL_MOUSE = 2;
constexpr uint8_t REQUEST_SET_IDLE = 0x0A;
constexpr uint8_t REQUEST_SET_PROTOCOL = 0x0B;
constexpr uint16_t BOOT_PROTOCOL = 0;

constexpr uint16_t BUTTON_CODES[] = {DISPLAY_BUTTON_LEFT, DISPLAY_BUTTON_RIGHT, DISPLAY_BUTTON_MIDDLE};

enum class Kind : uint8_t { Keyboard, Mouse };

struct Interface {
    Device* device{};  // Null while the entry is free
    Kind kind{};
    input::BootKeyboard keyboard{};
    uint8_t buttons{};
};

class Registry {
public:
    static Interface* add(Device& device, Kind kind) {
        intr::Guard guard;
        for (Interface& interface : interfaces_) {
            if (interface.device)
                continue;
            interface = Interface{};
            interface.device = &device;
            interface.kind = kind;
            return &interface;
        }
        return nullptr;
    }

    // Releases what the departing device still held down, so no key or
    // button stays pressed, and frees its entries.
    static void remove(Device& device) {
        intr::Guard guard;
        for (Interface& state : interfaces_) {
            if (state.device != &device)
                continue;
            state.keyboard.release();
            for (unsigned int bit = 0; bit < 3; ++bit)
                if ((state.buttons & (1U << bit)) != 0)
                    cons::pointer_event(DISPLAY_EVENT_BUTTON, BUTTON_CODES[bit], 0);
            state = Interface{};
        }
    }

    static void keyboard_report(void* context, const uint8_t* data, size_t length) {
        static_cast<Interface*>(context)->keyboard.report(data, length);
    }

    // Boot mouse: buttons, X and Y deltas, then a wheel delta most mice add.
    static void mouse_report(void* context, const uint8_t* data, size_t length) {
        auto* state = static_cast<Interface*>(context);
        if (length < 3)
            return;
        const uint8_t buttons = data[0];
        for (unsigned int bit = 0; bit < 3; ++bit) {
            const bool was = (state->buttons & (1U << bit)) != 0;
            const bool now = (buttons & (1U << bit)) != 0;
            if (was != now)
                cons::pointer_event(DISPLAY_EVENT_BUTTON, BUTTON_CODES[bit], now ? 1 : 0);
        }
        state->buttons = buttons;
        const auto dx = static_cast<int8_t>(data[1]);
        const auto dy = static_cast<int8_t>(data[2]);
        if (dx != 0)
            cons::pointer_event(DISPLAY_EVENT_REL, DISPLAY_REL_X, dx);
        if (dy != 0)
            cons::pointer_event(DISPLAY_EVENT_REL, DISPLAY_REL_Y, dy);
        if (length >= 4 && data[3] != 0)
            cons::pointer_event(DISPLAY_EVENT_REL, DISPLAY_REL_WHEEL, static_cast<int8_t>(data[3]));
    }

    static void tick() {
        intr::Guard guard;
        const uint64_t now = trap::uptime_ms();
        for (Interface& state : interfaces_)
            if (state.device && state.kind == Kind::Keyboard)
                state.keyboard.tick(now);
    }

private:
    inline static sys::array<Interface, 8> interfaces_{};
};

}  // namespace

Error attach(Device& device, const InterfaceDescriptor& interface, const EndpointDescriptor* endpoints,
             size_t endpoint_count) {
    ENSURE(interface.interface_class == CLASS_HID && interface.interface_subclass == SUBCLASS_BOOT &&
               (interface.interface_protocol == PROTOCOL_KEYBOARD || interface.interface_protocol == PROTOCOL_MOUSE),
           Error::NotSupported);
    const EndpointDescriptor* report = nullptr;
    for (size_t i = 0; i < endpoint_count; ++i)
        if ((endpoints[i].address & ENDPOINT_IN) != 0 &&
            (endpoints[i].attributes & ENDPOINT_TRANSFER_MASK) == ENDPOINT_INTERRUPT)
            report = &endpoints[i];
    ENSURE(report, Error::NotSupported);

    const uint8_t request = REQUEST_CLASS | REQUEST_INTERFACE;
    TRY(device.host->control(device, {request, REQUEST_SET_PROTOCOL, BOOT_PROTOCOL, interface.number, 0}, nullptr));
    // Report only on change; some mice reject the request, which is harmless.
    static_cast<void>(device.host->control(device, {request, REQUEST_SET_IDLE, 0, interface.number, 0}, nullptr));

    const bool keyboard = interface.interface_protocol == PROTOCOL_KEYBOARD;
    Interface* state = Registry::add(device, keyboard ? Kind::Keyboard : Kind::Mouse);
    ENSURE(state, Error::Full);
    Error error =
        device.host->start_in(device, *report, keyboard ? Registry::keyboard_report : Registry::mouse_report, state);
    if (error != Error::None) {
        Registry::remove(device);
        return error;
    }
    klog::info("usb %s: %s\n", location(device).text, keyboard ? "keyboard" : "mouse");
    return Error::None;
}

void detach(Device& device) {
    Registry::remove(device);
}

void tick() {
    Registry::tick();
}

}  // namespace usb::hid
