#pragma once

// HID input reports, as USB and Bluetooth keyboards and mice send them.
// Boot keyboard reports: modifier bits, a reserved byte and up to six
// pressed key usages; changes between reports become the same key events as
// other keyboards, and a held key repeats, as the keyboards themselves do
// not. Mouse reports, in the boot layout (button bits, X and Y movement, and
// the wheel most mice add) or in the layout a report descriptor gives,
// become pointer events.

#include <base/types.h>
#include "drivers/hid_report.h"

namespace input {

inline constexpr size_t BOOT_KEYBOARD_REPORT = 8;

// Boot keyboard usage IDs to the key codes of the display ABI; 0 = none.
uint16_t key_code(uint8_t usage);

class BootKeyboard {
public:
    // One report; shorter ones and the rollover report are ignored.
    void report(const uint8_t* data, size_t length);
    // Releases every key still held, as when the keyboard leaves.
    void release();
    // Repeats the newest held key once its delay has passed.
    void tick(uint64_t now_ms);

private:
    static constexpr size_t KEY_SLOTS = 6;

    uint8_t modifiers_{};
    uint8_t keys_[KEY_SLOTS]{};
    uint16_t repeat_code_{};
    uint64_t repeat_at_{};
};

class Mouse {
public:
    // One boot report; ones shorter than buttons and both movements are ignored.
    void boot_report(const uint8_t* data, size_t length);
    // One report in the layout of the mouse's report descriptor.
    void report(const MouseLayout& layout, const uint8_t* data, size_t length);
    // Releases buttons still held, as when the mouse leaves.
    void release();

private:
    static constexpr unsigned int BUTTONS = 3;

    void apply(uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel);

    uint8_t buttons_{};
};

}  // namespace input
