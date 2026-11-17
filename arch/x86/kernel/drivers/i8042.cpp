#include "drivers/i8042.h"
#include <kernel/log.h>

#include <asm/drivers/i8042.h>
#include <asm/drivers/i8259.h>

#include <base/types.h>
#include <asm/arch.h>

#include "drivers/i8259.h"
#include "drivers/intr.h"
#include "cons/cons.h"
#include "cons/keyboard.h"
#include "lib/stdio.h"

namespace {

class Keyboard {
public:
    static int getc() {
        if ((arch_port_read8(KBD_STATUS_REG) & KBD_OBF_FULL) == 0)
            return -1;
        uint8_t data = arch_port_read8(KBD_DATA_REG);
        if (pause_left_ != 0) {
            --pause_left_;
            return -1;
        }
        if (data == 0xE1) {
            pause_left_ = 5;
            return -1;
        }
        if (data == 0xE0) {
            extended_ = true;
            return -1;
        }
        bool pressed = (data & 0x80) == 0;
        uint16_t code = data & 0x7F;
        if (extended_) {
            extended_ = false;
            switch (code) {
                case 0x1C: code = 96; break;  // keypad Enter
                case 0x1D: code = 97; break;  // right Ctrl
                case 0x35: code = 98; break;  // keypad /
                case 0x48: code = 103; break;
                case 0x50: code = 108; break;
                case 0x4D: code = 106; break;
                case 0x4B: code = 105; break;
                case 0x47: code = 102; break;
                case 0x4F: code = 107; break;
                case 0x53: code = 111; break;
                default: return -1;
            }
        }
        return state_.translate(code, pressed);
    }

private:
    inline static cons::KeyboardState state_{};
    inline static bool extended_{};
    inline static uint8_t pause_left_{};
};

}  // namespace

namespace i8042 {

int init() {
    // PS/2 controller self-test: send 0xAA, expect 0x55
    arch_port_write8(KBD_STATUS_REG, 0xAA);

    int polls_left = 100000;
    while (polls_left-- > 0) {
        if (arch_port_read8(KBD_STATUS_REG) & KBD_OBF_FULL)
            break;
    }
    if (polls_left <= 0) {
        klog::warn("i8042: PS/2 controller self-test timeout\n");
        return -1;
    }

    uint8_t result = arch_port_read8(KBD_DATA_REG);
    if (result != 0x55) {
        klog::warn("i8042: PS/2 controller self-test failed (0x%02x)\n", result);
        return -1;
    }

    i8259::enable(IRQ_KBD);
    return 0;
}

int getc() {
    return Keyboard::getc();
}

void intr() {
    int c = getc();
    if (c < 0)
        return;

    cons::push_key(c);
}

}  // namespace i8042
