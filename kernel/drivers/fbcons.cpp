/**
 * @file fbcons.cpp
 * @brief Framebuffer console — renders text via a 32-bit linear framebuffer.
 *
 * Architecture-independent: the framebuffer physical address comes from
 * boot_info (set by both x86 and aarch64 UEFI bootloaders) and is mapped
 * into virtual memory via vmm::mmio_map() during late_init().
 *
 * Uses an embedded PSF bitmap font (linked via objcopy from fonts/console.psf).
 */

#include "drivers/fbcons.h"

#include <base/types.h>
#include <asm/page.h>
#include <kernel/bootinfo.h>
#include <kernel/psf.h>
#include "mm/vmm.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "drivers/intr.h"

// Embedded PSF font (linked via objcopy from fonts/console.psf)
extern "C" const uint8_t _binary_fonts_console_psf_start[];
extern "C" const uint8_t _binary_fonts_console_psf_end[];

extern struct BootInfo __kernel_boot_info;

namespace fbcons {

// =========================================================================
// PSF bitmap font — parsed at init() from the embedded .psf blob.
// =========================================================================
static psf::Font font;
static int font_width = 8;
static int font_height = 16;

// Framebuffer state
static uint32_t* fb_base = nullptr;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;  // bytes per scanline

// Text cursor
static uint32_t cols = 0;
static uint32_t rows = 0;
static uint32_t cur_x = 0;
static uint32_t cur_y = 0;

// Colors (32-bit XRGB)
static constexpr uint32_t FG_COLOR = 0x00AAAAAA;  // light grey
static constexpr uint32_t BG_COLOR = 0x00000000;  // black

static bool active = false;

// Early boot log: buffer output before framebuffer is mapped
static constexpr int EARLY_LOG_SIZE = 8192;
static char early_log[EARLY_LOG_SIZE];
static int early_log_pos = 0;

// Cursor blinking state
static bool cursor_visible = true;
static uint32_t cursor_tick = 0;
static constexpr uint32_t CURSOR_BLINK_RATE = 50;     // toggle every 50 ticks (0.5s at 100Hz)
static constexpr uint32_t CURSOR_COLOR = 0x00AAAAAA;  // same as FG_COLOR

static void draw_cursor() {
    if (!active)
        return;
    uint32_t x0 = cur_x * font_width;
    uint32_t y0 = cur_y * font_height + (font_height - 2);
    for (int row = 0; row < 2; row++) {
        uint32_t* pixel = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base) + (y0 + row) * fb_pitch) + x0;
        for (int col = 0; col < font_width; col++)
            pixel[col] = CURSOR_COLOR;
    }
}

static void erase_cursor() {
    if (!active)
        return;
    uint32_t x0 = cur_x * font_width;
    uint32_t y0 = cur_y * font_height + (font_height - 2);
    for (int row = 0; row < 2; row++) {
        uint32_t* pixel = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base) + (y0 + row) * fb_pitch) + x0;
        for (int col = 0; col < font_width; col++)
            pixel[col] = BG_COLOR;
    }
}

static void draw_char(uint32_t cx, uint32_t cy, int ch) {
    const uint8_t* gl = psf::glyph(&font, ch);
    uint32_t x0 = cx * font_width;
    uint32_t y0 = cy * font_height;
    for (int row = 0; row < font_height; row++) {
        uint8_t bits = gl[row];
        uint32_t* pixel = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base) + (y0 + row) * fb_pitch) + x0;
        for (int col = 0; col < font_width; col++) {
            *pixel++ = (bits & 0x80) ? FG_COLOR : BG_COLOR;
            bits <<= 1;
        }
    }
}

static void clear_row(uint32_t cy) {
    uint32_t y0 = cy * font_height;
    for (int row = 0; row < font_height; row++)
        memset(reinterpret_cast<uint8_t*>(fb_base) + (y0 + row) * fb_pitch, 0, fb_width * 4);
}

static void scroll_up() {
    uint32_t bytes_per_char_row = fb_pitch * font_height;
    uint8_t* dst = reinterpret_cast<uint8_t*>(fb_base);
    uint8_t* src = dst + bytes_per_char_row;
    uint32_t total = bytes_per_char_row * (rows - 1);
    memmove(dst, src, total);
    clear_row(rows - 1);
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) {
    if (bpp != 32 || fb_vaddr == 0 || width == 0 || height == 0)
        return;

    if (!psf::parse(_binary_fonts_console_psf_start, &font))
        return;
    font_width = font.width;
    font_height = font.height;

    fb_base = reinterpret_cast<uint32_t*>(fb_vaddr);
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;

    cols = width / font_width;
    rows = height / font_height;
    cur_x = 0;
    cur_y = 0;

    // Clear screen
    for (uint32_t y = 0; y < height; y++) {
        uint32_t* pixel = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base) + y * fb_pitch);
        for (uint32_t x = 0; x < width; x++)
            pixel[x] = BG_COLOR;
    }

    active = true;

    // Replay buffered early boot output
    for (int i = 0; i < early_log_pos; i++)
        putc(early_log[i]);
    early_log_pos = 0;
}

void putc(int c) {
    // Cursor coordinates temporarily pass the last row before scrolling. Timer
    // blinking and another task's output must not observe that intermediate state.
    intr::Guard guard;
    if (!active) {
        if (early_log_pos < EARLY_LOG_SIZE)
            early_log[early_log_pos++] = static_cast<char>(c);
        return;
    }

    if (cursor_visible)
        erase_cursor();

    switch (c & 0xFF) {
        case '\n':
            cur_x = 0;
            cur_y++;
            break;
        case '\r': cur_x = 0; break;
        case '\b':
            if (cur_x > 0) {
                cur_x--;
                draw_char(cur_x, cur_y, ' ');
            }
            break;
        case '\t':
            cur_x = (cur_x + 8) & ~7u;
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

    if (cur_y >= rows) {
        scroll_up();
        cur_y = rows - 1;
    }

    cursor_visible = true;
    cursor_tick = 0;
    draw_cursor();
}

void tick() {
    intr::Guard guard;
    if (!active)
        return;
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
    struct BootInfo* bi = &::__kernel_boot_info;

    cprintf("fbcons_late_init: type=%d addr=0x%lx w=%d h=%d pitch=%d bpp=%d\n", bi->framebuffer_type,
            static_cast<unsigned long>(bi->framebuffer_pa), bi->framebuffer_width, bi->framebuffer_height,
            bi->framebuffer_pitch, bi->framebuffer_bpp);

    if (bi->framebuffer_type != 1 || bi->framebuffer_pa == 0) {
        cprintf("fbcons: no framebuffer available\n");
        return;
    }

    uint64_t fb_phys = bi->framebuffer_pa;
    uint32_t fb_size = bi->framebuffer_pitch * bi->framebuffer_height;

    uintptr_t fb_va = vmm::mmio_map(static_cast<uintptr_t>(fb_phys), fb_size, VM_WRITE | VM_NOCACHE);
    if (fb_va == 0) {
        cprintf("fbcons: mmio_map failed for phys 0x%lx size 0x%x\n", static_cast<unsigned long>(fb_phys), fb_size);
        return;
    }

    cprintf("fbcons: phys 0x%lx -> virt 0x%lx (%dx%d)\n", static_cast<unsigned long>(fb_phys),
            static_cast<unsigned long>(fb_va), bi->framebuffer_width, bi->framebuffer_height);

    init(fb_va, bi->framebuffer_width, bi->framebuffer_height, bi->framebuffer_pitch, bi->framebuffer_bpp);
}

}  // namespace fbcons
