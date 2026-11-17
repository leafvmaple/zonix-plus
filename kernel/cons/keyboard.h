#pragma once

#include <base/types.h>

namespace cons {

// PC set-1 base keys and VirtIO input use the same key numbers here. Hardware
// backends normalize extended keys and pass both press and release events.
class KeyboardState {
public:
    int translate(uint16_t code, bool pressed) {
        switch (code) {
            case 42: left_shift_ = pressed; return -1;
            case 54: right_shift_ = pressed; return -1;
            case 29: left_ctrl_ = pressed; return -1;
            case 97: right_ctrl_ = pressed; return -1;
            default: break;
        }
        if (!pressed || code >= sizeof(NORMAL))
            return -1;
        char c = NORMAL[code];
        if (c == 0)
            return -1;
        if ((left_shift_ || right_shift_) && code < 71) {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            else {
                for (size_t i = 0; i + 1 < sizeof(SHIFT_FROM); ++i) {
                    if (c == SHIFT_FROM[i]) {
                        c = SHIFT_TO[i];
                        break;
                    }
                }
            }
        }
        if (left_ctrl_ || right_ctrl_) {
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 1;
            if (c >= '@' && c <= '_')
                return c - '@';
            if (c == ' ')
                return 0;
            if (c == '?')
                return 127;
            return -1;
        }
        return c;
    }

private:
    static constexpr char NORMAL[128] = {
        0,    0x1B, '1', '2',  '3', '4', '5', '6', '7',  '8', '9', '0', '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',
        't',  'y',  'u', 'i',  'o', 'p', '[', ']', '\n', 0,   'a', 's', 'd', 'f', 'g',  'h',  'j',  'k', 'l', ';',
        '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b',  'n', 'm', ',', '.', '/', 0,    '*',  0,    ' ', 0,   0,
        0,    0,    0,   0,    0,   0,   0,   0,   0,    0,   0,   '7', '8', '9', '-',  '4',  '5',  '6', '+', '1',
        '2',  '3',  '0', '.',  0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    '\n', 0,   '/',
    };
    static constexpr char SHIFT_FROM[] = "1234567890-=[];'`,./\\";
    static constexpr char SHIFT_TO[] = "!@#$%^&*()_+{}:\"~<>?|";
    static_assert(sizeof(SHIFT_FROM) == sizeof(SHIFT_TO));
    bool left_shift_{};
    bool right_shift_{};
    bool left_ctrl_{};
    bool right_ctrl_{};
};

}  // namespace cons
