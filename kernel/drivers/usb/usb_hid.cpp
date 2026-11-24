#include "drivers/usb/usb_hid.h"
#include "cons/cons.h"
#include "drivers/intr.h"
#include "lib/memory.h"
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

constexpr size_t KEYBOARD_REPORT = 8;  // Modifiers, reserved, six key usages
constexpr size_t KEY_SLOTS = 6;
constexpr uint8_t USAGE_ROLLOVER = 0x01;  // Too many keys down at once
constexpr uint16_t BUTTON_CODES[] = {DISPLAY_BUTTON_LEFT, DISPLAY_BUTTON_RIGHT, DISPLAY_BUTTON_MIDDLE};
// Left/right Control, Shift, Alt and Meta, in modifier-bit order.
constexpr uint16_t MODIFIER_CODES[] = {29, 42, 56, 125, 97, 54, 100, 126};

constexpr uint64_t REPEAT_DELAY_MS = 500;
constexpr uint64_t REPEAT_INTERVAL_MS = 33;

// HID usage page 7 (keyboard) IDs 0x04-0x65 to Linux input key codes.
constexpr uint8_t FIRST_USAGE = 0x04;
constexpr uint8_t KEY_CODES[] = {
    30, 48,  46,  32,  18,  33,  34,  35,  23,  36,  37,  38,  50,  49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45,  // a-x
    21, 44,                                                                                                      // y, z
    2,  3,   4,   5,   6,   7,   8,   9,   10,  11,                                                              // 1-0
    28, 1,   14,  15,  57,  12,  13,  26,  27,  43,  43,  39,  40,  41, 51, 52, 53, 58,  // Enter ... Caps Lock
    59, 60,  61,  62,  63,  64,  65,  66,  67,  68,  87,  88,                            // F1-F12
    99, 70,  119, 110, 102, 104, 111, 107, 109, 106, 105, 108, 103,                      // Print ... Up
    69, 98,  55,  74,  78,  96,  79,  80,  81,  75,  76,  77,  71,  72, 73, 82, 83,      // Keypad
    86, 127,                                                                             // 102nd key, Compose
};

enum class Kind : uint8_t { Keyboard, Mouse };

struct Interface {
    Device* device{};  // Null while the entry is free
    Kind kind{};
    uint8_t modifiers{};
    uint8_t keys[KEY_SLOTS]{};
    uint8_t buttons{};
    uint16_t repeat_code{};
    uint64_t repeat_at{};
};

bool contains(const uint8_t* keys, uint8_t usage) {
    for (size_t i = 0; i < KEY_SLOTS; ++i)
        if (keys[i] == usage)
            return true;
    return false;
}

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
            for (unsigned int bit = 0; bit < 8; ++bit)
                if ((state.modifiers & (1U << bit)) != 0)
                    cons::key_event(MODIFIER_CODES[bit], DISPLAY_KEY_RELEASE);
            for (uint8_t usage : state.keys)
                if (uint16_t code = key_code(usage); code != 0)
                    cons::key_event(code, DISPLAY_KEY_RELEASE);
            for (unsigned int bit = 0; bit < 3; ++bit)
                if ((state.buttons & (1U << bit)) != 0)
                    cons::pointer_event(DISPLAY_EVENT_BUTTON, BUTTON_CODES[bit], 0);
            state = Interface{};
        }
    }

    static void keyboard_report(void* context, const uint8_t* data, size_t length) {
        auto* state = static_cast<Interface*>(context);
        if (length < KEYBOARD_REPORT || data[2] == USAGE_ROLLOVER)
            return;
        const uint8_t modifiers = data[0];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            const bool was = (state->modifiers & (1U << bit)) != 0;
            const bool now = (modifiers & (1U << bit)) != 0;
            if (was != now)
                cons::key_event(MODIFIER_CODES[bit], now ? DISPLAY_KEY_PRESS : DISPLAY_KEY_RELEASE);
        }
        state->modifiers = modifiers;
        const uint8_t* keys = data + 2;
        for (size_t i = 0; i < KEY_SLOTS; ++i) {
            const uint8_t usage = state->keys[i];
            if (usage != 0 && !contains(keys, usage)) {
                const uint16_t code = key_code(usage);
                if (code != 0)
                    cons::key_event(code, DISPLAY_KEY_RELEASE);
                if (code == state->repeat_code)
                    state->repeat_code = 0;
            }
        }
        for (size_t i = 0; i < KEY_SLOTS; ++i) {
            const uint8_t usage = keys[i];
            if (usage == 0 || contains(state->keys, usage))
                continue;
            const uint16_t code = key_code(usage);
            if (code == 0)
                continue;
            cons::key_event(code, DISPLAY_KEY_PRESS);
            state->repeat_code = code;
            state->repeat_at = trap::uptime_ms() + REPEAT_DELAY_MS;
        }
        memcpy(state->keys, keys, KEY_SLOTS);
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
        for (Interface& state : interfaces_) {
            if (!state.device || state.kind != Kind::Keyboard || state.repeat_code == 0 || now < state.repeat_at)
                continue;
            cons::key_event(state.repeat_code, DISPLAY_KEY_REPEAT);
            state.repeat_at = now + REPEAT_INTERVAL_MS;
        }
    }

private:
    inline static sys::array<Interface, 8> interfaces_{};
};

}  // namespace

uint16_t key_code(uint8_t usage) {
    if (usage < FIRST_USAGE || usage - FIRST_USAGE >= sizeof(KEY_CODES))
        return 0;
    return KEY_CODES[usage - FIRST_USAGE];
}

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
    Error error = device.host->start_interrupt_in(device, *report,
                                                  keyboard ? Registry::keyboard_report : Registry::mouse_report, state);
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
