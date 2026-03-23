#include "drivers/i8042.h"

#include <asm/drivers/i8042.h>
#include <asm/drivers/i8259.h>

#include <base/types.h>
#include <asm/arch.h>

#include "drivers/i8259.h"
#include "drivers/intr.h"
#include "sched/sched.h"
#include "lib/waitqueue.h"
#include "lib/stdio.h"

namespace {

constexpr size_t KBD_BUF_SIZE = 128;

char kbd_buf[KBD_BUF_SIZE]{};
volatile int kbd_read{};
volatile int kbd_write{};
WaitQueue kbd_waitq{};  // Replaces ad-hoc kbd_waiter pointer

}  // namespace

static uint8_t normal_map[256] = {
    NO,   0x1B, '1', '2',  '3',  '4', '5',  '6',                                           // 0x00
    '7',  '8',  '9', '0',  '-',  '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',  // 0x10
    'o',  'p',  '[', ']',  '\n', NO,  'a',  's',  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  // 0x20
    '\'', '`',  NO,  '\\', 'z',  'x', 'c',  'v',  'b', 'n', 'm', ',', '.', '/', NO,  '*',  // 0x30
    NO,   ' ',  NO,  NO,   NO,   NO,  NO,   NO,   NO,  NO,  NO,  NO,  NO,  NO,  NO,  '7',  // 0x40
    '8',  '9',  '-', '4',  '5',  '6', '+',  '1',  '2', '3', '0', '.', NO,  NO,  NO,  NO,   // 0x50
};

static bool normal_map_initialized = false;

static void init_normal_map(void) {
    if (normal_map_initialized) {
        return;
    }

    normal_map_initialized = true;
    normal_map[0xC7] = KEY_HOME;
    normal_map[0x9C] = KP_ENTER;
    normal_map[0xB5] = KP_DIV;
    normal_map[0xC8] = KEY_UP;
    normal_map[0xC9] = KEY_PGUP;
    normal_map[0xCB] = KEY_LF;
    normal_map[0xCD] = KEY_RT;
    normal_map[0xCF] = KEY_END;
    normal_map[0xD0] = KEY_DN;
    normal_map[0xD1] = KEY_PGDN;
    normal_map[0xD2] = KEY_INS;
    normal_map[0xD3] = KEY_DEL;
}

namespace i8042 {

int init() {
    init_normal_map();

    // PS/2 controller self-test: send 0xAA, expect 0x55
    arch_port_outb(KBD_STATUS_REG, 0xAA);

    int timeout = 100000;
    while (timeout-- > 0) {
        if (arch_port_inb(KBD_STATUS_REG) & KBD_OBF_FULL)
            break;
    }
    if (timeout <= 0) {
        cprintf("i8042: PS/2 controller self-test timeout\n");
        return -1;
    }

    uint8_t result = arch_port_inb(KBD_DATA_REG);
    if (result != 0x55) {
        cprintf("i8042: PS/2 controller self-test failed (0x%02x)\n", result);
        return -1;
    }

    i8259::enable(IRQ_KBD);
    return 0;
}

int getc() {
    init_normal_map();
    if ((arch_port_inb(KBD_STATUS_REG) & KBD_OBF_FULL) == 0)
        return -1;

    uint8_t data = arch_port_inb(KBD_DATA_REG);

    // Ignore key release events (scancode & 0x80)
    if (data & 0x80) {
        return -1;
    }

    return normal_map[data];
}

void intr() {
    int c = getc();
    if (c <= 0)
        return;

    int next = (kbd_write + 1) % KBD_BUF_SIZE;
    if (next == kbd_read)
        return;  // buffer full, drop

    kbd_buf[kbd_write] = (char)c;
    kbd_write = next;

    kbd_waitq.wakeup_one();
}

// Blocking read — called from process context, sleeps until input available
char getc_blocking() {
    while (true) {
        {
            intr::Guard guard;
            if (kbd_read != kbd_write) {
                char c = kbd_buf[kbd_read];
                kbd_read = (kbd_read + 1) % KBD_BUF_SIZE;
                return c;
            }
        }
        kbd_waitq.sleep();
    }
}

}  // namespace i8042
