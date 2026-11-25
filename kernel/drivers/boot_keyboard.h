#pragma once

// HID boot-protocol keyboard reports, as USB and Bluetooth keyboards send
// them: modifier bits, a reserved byte and up to six pressed key usages.
// Changes between reports become the same key events as other keyboards,
// and a held key repeats, as the keyboards themselves do not.

#include <base/types.h>

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

}  // namespace input
