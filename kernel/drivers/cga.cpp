#include "cga.h"

#include <kernel/config.h>
#ifdef CONFIG_CGA

#include <base/types.h>
#include <asm/arch.h>
#include <asm/segments.h>

#include "cons_defs.h"

namespace cga {

// CGA hardware registers
constexpr uint16_t CGA_IDX_REG  = 0x3D4;
constexpr uint16_t CGA_DATA_REG = 0x3D5;

// CRTC cursor position registers
constexpr uint8_t CRTC_CURSOR_HIGH = 0x0E;
constexpr uint8_t CRTC_CURSOR_LOW  = 0x0F;

// CGA memory buffer address (physical 0xB8000)
constexpr uintptr_t CGA_BUF = 0xB8000;

// Character with default attribute (white on black)
constexpr uint16_t CRT_ERASE_CHAR = 0x0720;

uint16_t* crt_buf = reinterpret_cast<uint16_t*>(CGA_BUF + KERNEL_BASE);
static uint16_t crt_pos = 0;

static void cur_update() {
    arch_port_outb(CGA_IDX_REG, CRTC_CURSOR_HIGH);
    arch_port_outb(CGA_DATA_REG, crt_pos >> 8);
    arch_port_outb(CGA_IDX_REG, CRTC_CURSOR_LOW);
    arch_port_outb(CGA_DATA_REG, crt_pos);
}

void init() {
    arch_port_outb(CGA_IDX_REG, CRTC_CURSOR_HIGH);
    crt_pos = arch_port_inb(CGA_DATA_REG) << 8;
    arch_port_outb(CGA_IDX_REG, CRTC_CURSOR_LOW);
    crt_pos |= arch_port_inb(CGA_DATA_REG);
}

void putc(int c) {
    c |= 0x0700;

    switch (c & 0xFF) {
    case '\b':
        if (crt_pos > 0) {
            crt_pos--;
            crt_buf[crt_pos] = (c & ~0xff) | ' ';
        }
        break;
    case '\n':
        crt_pos += CRT_COLS;
        if (crt_pos / CRT_COLS >= CRT_ROWS)
            scrup();
    case '\r':
        crt_pos -= (crt_pos % CRT_COLS);
        break;
    default:
        crt_buf[crt_pos++] = c;     // write the character
        break;
    }

    cur_update();
}

void scrup() {
    uint16_t *source = crt_buf + CRT_COLS;
    int count = (CRT_ROWS - 1) * CRT_COLS;

    for (int i = 0; i < count; i++)
        crt_buf[i] = source[i];

    // 使用 `memset` 模拟 `rep stosw`
    uint16_t *clear_start = crt_buf + CRT_COLS * (CRT_ROWS - 1);
    for (int i = 0; i < CRT_COLS; i++) {
        clear_start[i] = CRT_ERASE_CHAR;
    }

    crt_pos -= CRT_COLS;
}

} // namespace cga

#endif // CONFIG_CGA