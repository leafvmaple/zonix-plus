#include "drivers/boot_keyboard.h"
#include "cons/cons.h"
#include "lib/memory.h"
#include "trap/trap.h"
#include <abi/display.h>

namespace input {
namespace {

constexpr uint8_t USAGE_ROLLOVER = 0x01;  // Too many keys down at once
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

bool contains(const uint8_t* keys, size_t count, uint8_t usage) {
    for (size_t i = 0; i < count; ++i)
        if (keys[i] == usage)
            return true;
    return false;
}

}  // namespace

uint16_t key_code(uint8_t usage) {
    if (usage < FIRST_USAGE || usage - FIRST_USAGE >= sizeof(KEY_CODES))
        return 0;
    return KEY_CODES[usage - FIRST_USAGE];
}

void BootKeyboard::report(const uint8_t* data, size_t length) {
    if (length < BOOT_KEYBOARD_REPORT || data[2] == USAGE_ROLLOVER)
        return;
    const uint8_t modifiers = data[0];
    for (unsigned int bit = 0; bit < 8; ++bit) {
        const bool was = (modifiers_ & (1U << bit)) != 0;
        const bool now = (modifiers & (1U << bit)) != 0;
        if (was != now)
            cons::key_event(MODIFIER_CODES[bit], now ? DISPLAY_KEY_PRESS : DISPLAY_KEY_RELEASE);
    }
    modifiers_ = modifiers;
    const uint8_t* keys = data + 2;
    for (size_t i = 0; i < KEY_SLOTS; ++i) {
        const uint8_t usage = keys_[i];
        if (usage != 0 && !contains(keys, KEY_SLOTS, usage)) {
            const uint16_t code = key_code(usage);
            if (code != 0)
                cons::key_event(code, DISPLAY_KEY_RELEASE);
            if (code == repeat_code_)
                repeat_code_ = 0;
        }
    }
    for (size_t i = 0; i < KEY_SLOTS; ++i) {
        const uint8_t usage = keys[i];
        if (usage == 0 || contains(keys_, KEY_SLOTS, usage))
            continue;
        const uint16_t code = key_code(usage);
        if (code == 0)
            continue;
        cons::key_event(code, DISPLAY_KEY_PRESS);
        repeat_code_ = code;
        repeat_at_ = trap::uptime_ms() + REPEAT_DELAY_MS;
    }
    memcpy(keys_, keys, KEY_SLOTS);
}

void BootKeyboard::release() {
    for (unsigned int bit = 0; bit < 8; ++bit)
        if ((modifiers_ & (1U << bit)) != 0)
            cons::key_event(MODIFIER_CODES[bit], DISPLAY_KEY_RELEASE);
    for (uint8_t usage : keys_)
        if (uint16_t code = key_code(usage); code != 0)
            cons::key_event(code, DISPLAY_KEY_RELEASE);
    *this = BootKeyboard{};
}

void BootKeyboard::tick(uint64_t now_ms) {
    if (repeat_code_ == 0 || now_ms < repeat_at_)
        return;
    cons::key_event(repeat_code_, DISPLAY_KEY_REPEAT);
    repeat_at_ = now_ms + REPEAT_INTERVAL_MS;
}

}  // namespace input
