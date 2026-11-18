#include "drivers/i8042.h"
#include <kernel/log.h>

#include <asm/drivers/i8042.h>
#include <asm/drivers/i8259.h>

#include <base/types.h>
#include <asm/arch.h>

#include "drivers/i8259.h"
#include "drivers/intr.h"
#include "cons/cons.h"
#include <abi/display.h>

namespace {

constexpr uint8_t AUX_DATA = 0x20;
constexpr uint8_t AUX_IRQ_ENABLE = 0x02;
constexpr uint8_t AUX_CLOCK_DISABLED = 0x20;
constexpr uint8_t READ_CONFIG = 0x20;
constexpr uint8_t WRITE_CONFIG = 0x60;
constexpr uint8_t ENABLE_AUX = 0xA8;
constexpr uint8_t WRITE_AUX = 0xD4;
constexpr uint8_t MOUSE_DEFAULTS = 0xF6;
constexpr uint8_t MOUSE_STREAM_ON = 0xF4;
constexpr uint8_t MOUSE_ACK = 0xFA;

bool write_port(uint16_t port, uint8_t byte) {
    for (int polls = 0; polls < 100000; ++polls) {
        if ((arch_port_read8(KBD_STATUS_REG) & KBD_IBF_FULL) == 0) {
            arch_port_write8(port, byte);
            return true;
        }
    }
    return false;
}

class Keyboard {
public:
    static void receive(uint8_t data) {
        if (pause_left_ != 0) {
            if (--pause_left_ == 0) {
                cons::key_event(119, DISPLAY_KEY_PRESS);
                cons::key_event(119, DISPLAY_KEY_RELEASE);
            }
            return;
        }
        if (data == 0xE1) {
            pause_left_ = 5;
            return;
        }
        if (data == 0xE0) {
            extended_ = true;
            return;
        }
        bool pressed = (data & 0x80) == 0;
        uint16_t code = data & 0x7F;
        bool extended = extended_;
        extended_ = false;
        if (extended) {
            switch (code) {
                case 0x1C: code = 96; break;   // keypad Enter
                case 0x1D: code = 97; break;   // right Ctrl
                case 0x35: code = 98; break;   // keypad /
                case 0x37: code = 99; break;   // Print Screen
                case 0x38: code = 100; break;  // right Alt
                case 0x49: code = 104; break;
                case 0x51: code = 109; break;
                case 0x52: code = 110; break;
                case 0x5B: code = 125; break;  // left Meta
                case 0x5C: code = 126; break;  // right Meta
                case 0x5D: code = 127; break;  // Menu
                case 0x48: code = 103; break;
                case 0x50: code = 108; break;
                case 0x4D: code = 106; break;
                case 0x4B: code = 105; break;
                case 0x47: code = 102; break;
                case 0x4F: code = 107; break;
                case 0x53: code = 111; break;
                default: return;
            }
        }
        if (extended || (code != 0 && code < 0x59))
            cons::key_event(code, pressed ? DISPLAY_KEY_PRESS : DISPLAY_KEY_RELEASE);
    }

private:
    inline static bool extended_{};
    inline static uint8_t pause_left_{};
};

class Mouse {
public:
    static bool init() {
        if (!write_port(KBD_STATUS_REG, READ_CONFIG))
            return false;
        int config = response(false);
        if (config < 0)
            return false;
        // Poll the auxiliary replies before unmasking the mouse IRQ.
        config &= ~(AUX_IRQ_ENABLE | AUX_CLOCK_DISABLED);
        return configure(config) && write_port(KBD_STATUS_REG, ENABLE_AUX) && command(MOUSE_DEFAULTS) &&
               command(MOUSE_STREAM_ON) && configure(config | AUX_IRQ_ENABLE);
    }
    static void receive(uint8_t data) {
        if (length_ == 0 && (data & 0x08) == 0)
            return;
        packet_[length_++] = data;
        if (length_ != 3)
            return;
        length_ = 0;
        uint8_t flags = packet_[0];
        if ((flags & 0xC0) == 0) {
            int dx = packet_[1] - ((flags & 0x10) != 0 ? 256 : 0);
            int dy = packet_[2] - ((flags & 0x20) != 0 ? 256 : 0);
            if (dx != 0)
                cons::pointer_event(DISPLAY_EVENT_REL, DISPLAY_REL_X, dx);
            if (dy != 0)
                cons::pointer_event(DISPLAY_EVENT_REL, DISPLAY_REL_Y, -dy);
        }
        unsigned int buttons = flags & 7;
        for (unsigned int i = 0; i < 3; ++i) {
            unsigned int bit = 1U << i;
            if (((buttons ^ buttons_) & bit) != 0)
                cons::pointer_event(DISPLAY_EVENT_BUTTON, DISPLAY_BUTTON_LEFT + i,
                                    (buttons & bit) != 0 ? DISPLAY_KEY_PRESS : DISPLAY_KEY_RELEASE);
        }
        buttons_ = buttons;
    }

private:
    static int response(bool auxiliary) {
        for (int polls = 0; polls < 100000; ++polls) {
            uint8_t status = arch_port_read8(KBD_STATUS_REG);
            if ((status & KBD_OBF_FULL) == 0)
                continue;
            uint8_t data = arch_port_read8(KBD_DATA_REG);
            if (((status & AUX_DATA) != 0) == auxiliary)
                return data;
            if (!auxiliary)
                continue;
            Keyboard::receive(data);
        }
        return -1;
    }
    static bool configure(uint8_t config) {
        return write_port(KBD_STATUS_REG, WRITE_CONFIG) && write_port(KBD_DATA_REG, config);
    }
    static bool command(uint8_t command) {
        return write_port(KBD_STATUS_REG, WRITE_AUX) && write_port(KBD_DATA_REG, command) &&
               response(true) == MOUSE_ACK;
    }

    inline static uint8_t packet_[3]{};
    inline static unsigned int length_{};
    inline static unsigned int buttons_{};
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

    if (Mouse::init())
        i8259::enable(IRQ_MOUSE);
    else
        klog::warn("i8042: PS/2 mouse unavailable\n");
    i8259::enable(IRQ_KBD);
    return 0;
}

void intr() {
    uint8_t status = arch_port_read8(KBD_STATUS_REG);
    if ((status & KBD_OBF_FULL) == 0)
        return;
    uint8_t data = arch_port_read8(KBD_DATA_REG);
    if ((status & AUX_DATA) != 0)
        Mouse::receive(data);
    else
        Keyboard::receive(data);
}

}  // namespace i8042
