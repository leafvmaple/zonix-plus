#include "cga.h"

#include <base/types.h>
#include <arch/x86/io.h>
#include <arch/x86/segments.h>

#include "cons_defs.h"

namespace {

// CGA hardware registers
constexpr uint16_t CGA_IDX_REG  = 0x3D4;
constexpr uint16_t CGA_DATA_REG = 0x3D5;

// CRTC cursor position registers
constexpr uint8_t CRTC_CURSOR_HIGH = 0x0E;
constexpr uint8_t CRTC_CURSOR_LOW  = 0x0F;

// CGA memory buffer address
constexpr uintptr_t CGA_BUF = 0xB8000;

// Character with default attribute (white on black)
constexpr uint16_t CRT_ERASE_CHAR = 0x0720;

} // namespace

uint16_t* crt_buf = reinterpret_cast<uint16_t*>(CGA_BUF + KERNEL_BASE);
static uint16_t crt_pos = 0;

static void cur_update() {
    outb(CGA_IDX_REG, CRTC_CURSOR_HIGH);
    outb(CGA_DATA_REG, crt_pos >> 8);
    outb(CGA_IDX_REG, CRTC_CURSOR_LOW);
    outb(CGA_DATA_REG, crt_pos);
}

void cga_init() {
    outb(CGA_IDX_REG, CRTC_CURSOR_HIGH);
    crt_pos = inb(CGA_DATA_REG) << 8;
    outb(CGA_IDX_REG, CRTC_CURSOR_LOW);
    crt_pos |= inb(CGA_DATA_REG);
}

void cga_putc(int c) {
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
            cga_scrup();
    case '\r':
        crt_pos -= (crt_pos % CRT_COLS);
        break;
    default:
        crt_buf[crt_pos++] = c;     // write the character
        break;
    }

    cur_update();
}

void cga_scrup() {
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