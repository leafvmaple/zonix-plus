#include "fbcons.h"

#include <kernel/config.h>
#ifdef CONFIG_FBCONS

#include <base/types.h>
#include <asm/pg.h>
#include <kernel/bootinfo.h>
#include <kernel/psf.h>
#include "../mm/vmm.h"
#include "stdio.h"

extern struct boot_info __kernel_boot_info;

// Embedded PSF font (linked via objcopy from fonts/console.psf)
extern "C" const uint8_t _binary_fonts_console_psf_start[];
extern "C" const uint8_t _binary_fonts_console_psf_end[];

namespace fbcons {

// =========================================================================
// PSF bitmap font — parsed at init() from the embedded .psf blob.
// Replaces the old hardcoded font8x16[] array.
// To change the font, simply swap fonts/console.psf and rebuild.
// =========================================================================
static psf::Font font;
static int FONT_W = 8;
static int FONT_H = 16;
// Framebuffer state
static uint32_t* fb_base = nullptr;
static uint32_t  fb_width = 0;
static uint32_t  fb_height = 0;
static uint32_t  fb_pitch = 0;    // bytes per scanline

// Text cursor
static uint32_t cols = 0;      // characters per row
static uint32_t rows = 0;      // characters per column
static uint32_t cur_x = 0;
static uint32_t cur_y = 0;

// Colors (32-bit ARGB/XRGB)
static constexpr uint32_t FG_COLOR = 0x00AAAAAA;  // light grey
static constexpr uint32_t BG_COLOR = 0x00000000;  // black

static bool active = false;

// Early boot log: buffer output before framebuffer is mapped
static constexpr int EARLY_LOG_SIZE = 8192;
static char early_log[EARLY_LOG_SIZE];
static int  early_log_pos = 0;

// Cursor blinking state
static bool cursor_visible = true;
static uint32_t cursor_tick = 0;
static constexpr uint32_t CURSOR_BLINK_RATE = 50;  // toggle every 50 ticks (0.5s at 100Hz)
static constexpr uint32_t CURSOR_COLOR = 0x00AAAAAA;  // same as FG_COLOR

static void draw_cursor() {
    if (!active) return;
    // Draw underscore cursor: fill bottom 2 rows of the character cell
    uint32_t x0 = cur_x * FONT_W;
    uint32_t y0 = cur_y * FONT_H + (FONT_H - 2);  // last 2 pixel rows
    for (int row = 0; row < 2; row++) {
        uint32_t* pixel = (uint32_t*)((uint8_t*)fb_base + (y0 + row) * fb_pitch) + x0;
        for (int col = 0; col < FONT_W; col++) {
            pixel[col] = CURSOR_COLOR;
        }
    }
}

static void erase_cursor() {
    if (!active) return;
    uint32_t x0 = cur_x * FONT_W;
    uint32_t y0 = cur_y * FONT_H + (FONT_H - 2);
    for (int row = 0; row < 2; row++) {
        uint32_t* pixel = (uint32_t*)((uint8_t*)fb_base + (y0 + row) * fb_pitch) + x0;
        for (int col = 0; col < FONT_W; col++) {
            pixel[col] = BG_COLOR;
        }
    }
}

// -------------------------------------------------------------------------
static inline void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    // pitch is in bytes; each pixel is 4 bytes for 32-bpp
    uint32_t* row = (uint32_t*)((uint8_t*)fb_base + y * fb_pitch);
    row[x] = color;
}

static void draw_char(uint32_t cx, uint32_t cy, int ch) {
    const uint8_t* gl = psf::glyph(&font, ch);

    uint32_t x0 = cx * FONT_W;
    uint32_t y0 = cy * FONT_H;

    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = gl[row];
        uint32_t* pixel = (uint32_t*)((uint8_t*)fb_base + (y0 + row) * fb_pitch) + x0;
        for (int col = 0; col < FONT_W; col++) {
            *pixel++ = (bits & 0x80) ? FG_COLOR : BG_COLOR;
            bits <<= 1;
        }
    }
}

static void clear_row(uint32_t cy) {
    uint32_t y0 = cy * FONT_H;
    for (int row = 0; row < FONT_H; row++) {
        uint32_t* pixel = (uint32_t*)((uint8_t*)fb_base + (y0 + row) * fb_pitch);
        for (uint32_t x = 0; x < fb_width; x++) {
            pixel[x] = BG_COLOR;
        }
    }
}

static void scroll_up() {
    // Move all rows up by one character row (FONT_H pixels)
    uint32_t bytes_per_char_row = fb_pitch * FONT_H;
    uint8_t* dst = (uint8_t*)fb_base;
    uint8_t* src = dst + bytes_per_char_row;
    uint32_t total = bytes_per_char_row * (rows - 1);

    // Copy byte-by-byte (no memcpy available with guaranteed alignment)
    for (uint32_t i = 0; i < total; i++) {
        dst[i] = src[i];
    }

    // Clear last row
    clear_row(rows - 1);
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) {
    if (bpp != 32 || fb_vaddr == 0 || width == 0 || height == 0) return;

    // Parse the embedded PSF font
    if (!psf::parse(_binary_fonts_console_psf_start, &font)) return;
    FONT_W = font.width;
    FONT_H = font.height;

    fb_base   = (uint32_t*)fb_vaddr;
    fb_width  = width;
    fb_height = height;
    fb_pitch  = pitch;

    cols = width  / FONT_W;
    rows = height / FONT_H;
    cur_x = 0;
    cur_y = 0;

    // Clear screen
    for (uint32_t y = 0; y < height; y++) {
        uint32_t* pixel = (uint32_t*)((uint8_t*)fb_base + y * fb_pitch);
        for (uint32_t x = 0; x < width; x++) {
            pixel[x] = BG_COLOR;
        }
    }

    active = true;

    // Replay buffered early boot output
    for (int i = 0; i < early_log_pos; i++) {
        putc(early_log[i]);
    }
    early_log_pos = 0;
}

void putc(int c) {
    if (!active) {
        // Buffer output until framebuffer is ready
        if (early_log_pos < EARLY_LOG_SIZE)
            early_log[early_log_pos++] = (char)c;
        return;
    }

    // Erase old cursor before updating position
    if (cursor_visible) erase_cursor();

    switch (c & 0xFF) {
    case '\n':
        cur_x = 0;
        cur_y++;
        break;
    case '\r':
        cur_x = 0;
        break;
    case '\b':
        if (cur_x > 0) {
            cur_x--;
            draw_char(cur_x, cur_y, ' ');
        }
        break;
    case '\t':
        // Advance to next 8-column tab stop
        cur_x = (cur_x + 8) & ~7;
        if (cur_x >= cols) {
            cur_x = 0;
            cur_y++;
        }
        break;
    default:
        draw_char(cur_x, cur_y, c);
        cur_x++;
        if (cur_x >= cols) {
            cur_x = 0;
            cur_y++;
        }
        break;
    }

    // Scroll if needed
    if (cur_y >= rows) {
        scroll_up();
        cur_y = rows - 1;
    }

    // Redraw cursor at new position
    cursor_visible = true;
    cursor_tick = 0;
    draw_cursor();
}

void tick() {
    if (!active) return;
    cursor_tick++;
    if (cursor_tick >= CURSOR_BLINK_RATE) {
        cursor_tick = 0;
        if (cursor_visible) {
            erase_cursor();
            cursor_visible = false;
        } else {
            draw_cursor();
            cursor_visible = true;
        }
    }
}

bool is_active() {
    return active;
}

void late_init() {
    struct boot_info *bi = &::__kernel_boot_info;

    cprintf("fbcons_late_init: type=%d addr=0x%lx w=%d h=%d pitch=%d bpp=%d\n",
            bi->framebuffer_type, static_cast<unsigned long>(bi->framebuffer_addr),
            bi->framebuffer_width, bi->framebuffer_height,
            bi->framebuffer_pitch, bi->framebuffer_bpp);

    if (bi->framebuffer_type != 1 || bi->framebuffer_addr == 0) {
        cprintf("fbcons: no framebuffer available\n");
        return;
    }

    uint64_t fb_phys = bi->framebuffer_addr;
    uint32_t fb_size = bi->framebuffer_pitch * bi->framebuffer_height;

    // Map framebuffer into kernel virtual address space
    uintptr_t fb_va = mmio_map(static_cast<uintptr_t>(fb_phys), fb_size, PTE_W | PTE_PCD | PTE_PWT);

    cprintf("fbcons: phys 0x%lx -> virt 0x%lx (%dx%d)\n",
            static_cast<unsigned long>(fb_phys), static_cast<unsigned long>(fb_va),
            bi->framebuffer_width, bi->framebuffer_height);

    init(fb_va, bi->framebuffer_width, bi->framebuffer_height,
         bi->framebuffer_pitch, bi->framebuffer_bpp);
}

} // namespace fbcons

#endif // CONFIG_FBCONS
