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
#include <kernel/log.h>

#include <base/types.h>
#include <kernel/psf.h>
#include "drivers/display.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "drivers/intr.h"
#include <sys/array.hpp>

// Embedded PSF font (linked via objcopy from fonts/console.psf)
extern "C" const uint8_t _binary_fonts_console_psf_start[];
extern "C" const uint8_t _binary_fonts_console_psf_end[];


namespace fbcons {
namespace {
class Screen {
public:
    static void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) {
        if (bpp != 32 || fb_vaddr == 0 || width == 0 || height == 0)
            return;

        if (!psf::parse(_binary_fonts_console_psf_start, &font_))
            return;
        if (font_.width != 8 || font_.height < 2 || width < font_.width || height < font_.height ||
            static_cast<size_t>(pitch) < static_cast<size_t>(width) * sizeof(uint32_t))
            return;
        font_width_ = font_.width;
        font_height_ = font_.height;

        fb_base_ = reinterpret_cast<uint32_t*>(fb_vaddr);
        fb_width_ = width;
        fb_height_ = height;
        fb_pitch_ = pitch;

        cols_ = width / font_width_;
        rows_ = height / font_height_;
        cur_x_ = 0;
        cur_y_ = 0;

        // Clear screen
        for (uint32_t y = 0; y < height; y++) {
            uint32_t* pixel = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base_) + y * fb_pitch_);
            for (uint32_t x = 0; x < width; x++)
                pixel[x] = BG_COLOR;
        }

        active_ = true;
        cursor_enabled_ = true;
        cursor_visible_ = false;

        // Replay buffered early boot output
        for (int i = 0; i < early_log_pos_; i++)
            putc(early_log_[i]);
        early_log_pos_ = 0;
        if (!cursor_visible_) {
            cursor_visible_ = true;
            draw_cursor();
        }
    }

    static void putc(int c) {
        // Cursor coordinates temporarily pass the last row before scrolling. Timer
        // blinking and another task's output must not observe that intermediate state.
        intr::Guard guard;
        if (!active_) {
            if (early_log_pos_ < EARLY_LOG_SIZE)
                early_log_[early_log_pos_++] = static_cast<char>(c);
            return;
        }

        if (suspended_)
            return;
        if (cursor_visible_)
            erase_cursor();

        switch (c & 0xFF) {
            case '\n':
                cur_x_ = 0;
                cur_y_++;
                break;
            case '\r': cur_x_ = 0; break;
            case '\b':
                if (cur_x_ > 0) {
                    cur_x_--;
                    draw_char(cur_x_, cur_y_, ' ');
                }
                break;
            case '\t':
                cur_x_ = (cur_x_ + 8) & ~7u;
                if (cur_x_ >= cols_) {
                    cur_x_ = 0;
                    cur_y_++;
                }
                break;
            default:
                draw_char(cur_x_, cur_y_, c);
                cur_x_++;
                if (cur_x_ >= cols_) {
                    cur_x_ = 0;
                    cur_y_++;
                }
                break;
        }

        if (cur_y_ >= rows_) {
            scroll_up();
            cur_y_ = rows_ - 1;
        }

        cursor_visible_ = cursor_enabled_;
        cursor_tick_ = 0;
        draw_cursor();
    }

    static void tick() {
        intr::Guard guard;
        if (!active_ || !cursor_enabled_ || suspended_)
            return;
        cursor_tick_++;
        if (cursor_tick_ >= CURSOR_BLINK_RATE) {
            cursor_tick_ = 0;
            if (cursor_visible_) {
                erase_cursor();
                cursor_visible_ = false;
            } else {
                draw_cursor();
                cursor_visible_ = true;
            }
        }
    }

    static bool is_active() { return active_; }

    static TerminalSize size() { return {rows_, cols_}; }
    static void move_cursor(unsigned int row, unsigned int col) {
        intr::Guard guard;
        if (!active_ || suspended_)
            return;
        if (cursor_visible_)
            erase_cursor();
        cur_y_ = row < rows_ ? row : rows_ - 1;
        cur_x_ = col < cols_ ? col : cols_ - 1;
        cursor_tick_ = 0;
        if (cursor_enabled_) {
            cursor_visible_ = true;
            draw_cursor();
        }
    }
    static void move_column(unsigned int col) {
        intr::Guard guard;
        move_cursor(cur_y_, col);
    }
    static void clear() {
        intr::Guard guard;
        if (!active_ || suspended_)
            return;
        if (cursor_visible_)
            erase_cursor();
        for (uint32_t y = 0; y < fb_height_; ++y)
            memset(reinterpret_cast<uint8_t*>(fb_base_) + static_cast<size_t>(y) * fb_pitch_, 0, fb_width_ * 4);
        if (cursor_enabled_)
            draw_cursor();
    }
    static void erase_line(unsigned int mode) {
        intr::Guard guard;
        if (!active_ || suspended_)
            return;
        if (cursor_visible_)
            erase_cursor();
        uint32_t x = mode == 2 ? 0 : cur_x_ * font_width_;
        for (int row = 0; row < font_height_; ++row)
            memset(reinterpret_cast<uint8_t*>(fb_base_) +
                       (static_cast<size_t>(cur_y_) * font_height_ + row) * fb_pitch_ + x * 4,
                   0, (fb_width_ - x) * 4);
        if (cursor_enabled_)
            draw_cursor();
    }
    static void set_cursor_visible(bool visible) {
        intr::Guard guard;
        if (!active_ || suspended_)
            return;
        if (cursor_visible_)
            erase_cursor();
        cursor_enabled_ = visible;
        cursor_visible_ = visible;
        cursor_tick_ = 0;
        if (visible)
            draw_cursor();
    }

    static void suspend() {
        intr::Guard guard;
        if (cursor_visible_)
            erase_cursor();
        suspended_ = true;
    }
    static void resume() {
        intr::Guard guard;
        suspended_ = false;
    }

private:
    inline static bool suspended_{};
    inline static psf::Font font_{};
    inline static int font_width_ = 8;
    inline static int font_height_ = 16;

    // Framebuffer state
    inline static uint32_t* fb_base_ = nullptr;
    inline static uint32_t fb_width_ = 0;
    inline static uint32_t fb_height_ = 0;
    inline static uint32_t fb_pitch_ = 0;  // bytes per scanline

    // Text cursor
    inline static uint32_t cols_ = 0;
    inline static uint32_t rows_ = 0;
    inline static uint32_t cur_x_ = 0;
    inline static uint32_t cur_y_ = 0;

    // Colors (32-bit XRGB)
    static constexpr uint32_t FG_COLOR = 0x00AAAAAA;  // light grey
    static constexpr uint32_t BG_COLOR = 0x00000000;  // black

    inline static bool active_ = false;

    // Early boot log: buffer output before framebuffer is mapped
    static constexpr int EARLY_LOG_SIZE = 8192;
    inline static char early_log_[EARLY_LOG_SIZE]{};
    inline static int early_log_pos_ = 0;

    // Cursor blinking state
    inline static bool cursor_visible_ = true;
    inline static uint32_t cursor_tick_ = 0;
    static constexpr uint32_t CURSOR_BLINK_RATE = 50;     // toggle every 50 ticks (0.5s at 100Hz)
    static constexpr uint32_t CURSOR_COLOR = 0x00AAAAAA;  // same as FG_COLOR

    inline static bool cursor_enabled_ = true;
    inline static sys::array<uint32_t, 16> cursor_pixels_{};

    static void draw_cursor() {
        if (!active_ || !cursor_enabled_ || suspended_)
            return;
        cursor_visible_ = true;
        uint32_t x0 = cur_x_ * font_width_;
        uint32_t y0 = cur_y_ * font_height_ + (font_height_ - 2);
        for (int row = 0; row < 2; row++) {
            uint32_t* pixel =
                reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base_) + (y0 + row) * fb_pitch_) + x0;
            for (int col = 0; col < font_width_; col++) {
                cursor_pixels_[row * font_width_ + col] = pixel[col];
                pixel[col] = CURSOR_COLOR;
            }
        }
    }

    static void erase_cursor() {
        if (!active_ || suspended_)
            return;
        cursor_visible_ = false;
        uint32_t x0 = cur_x_ * font_width_;
        uint32_t y0 = cur_y_ * font_height_ + (font_height_ - 2);
        for (int row = 0; row < 2; row++) {
            uint32_t* pixel =
                reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base_) + (y0 + row) * fb_pitch_) + x0;
            for (int col = 0; col < font_width_; col++)
                pixel[col] = cursor_pixels_[row * font_width_ + col];
        }
    }

    static void draw_char(uint32_t cx, uint32_t cy, int ch) {
        const uint8_t* gl = psf::glyph(&font_, ch);
        uint32_t x0 = cx * font_width_;
        uint32_t y0 = cy * font_height_;
        for (int row = 0; row < font_height_; row++) {
            uint8_t bits = gl[row];
            uint32_t* pixel =
                reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb_base_) + (y0 + row) * fb_pitch_) + x0;
            for (int col = 0; col < font_width_; col++) {
                *pixel++ = (bits & 0x80) ? FG_COLOR : BG_COLOR;
                bits <<= 1;
            }
        }
    }

    static void clear_row(uint32_t cy) {
        uint32_t y0 = cy * font_height_;
        for (int row = 0; row < font_height_; row++)
            memset(reinterpret_cast<uint8_t*>(fb_base_) + (y0 + row) * fb_pitch_, 0, fb_width_ * 4);
    }

    static void scroll_up() {
        uint32_t bytes_per_char_row = fb_pitch_ * font_height_;
        uint8_t* dst = reinterpret_cast<uint8_t*>(fb_base_);
        uint8_t* src = dst + bytes_per_char_row;
        uint32_t total = bytes_per_char_row * (rows_ - 1);
        memmove(dst, src, total);
        clear_row(rows_ - 1);
    }
};
}  // namespace

void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) {
    Screen::init(fb_vaddr, width, height, pitch, bpp);
}
void putc(int c) {
    Screen::putc(c);
}
void tick() {
    Screen::tick();
}
bool is_active() {
    return Screen::is_active();
}
TerminalSize size() {
    return Screen::size();
}
void move_cursor(unsigned int row, unsigned int col) {
    Screen::move_cursor(row, col);
}
void move_column(unsigned int col) {
    Screen::move_column(col);
}
void clear() {
    Screen::clear();
}
void erase_line(unsigned int mode) {
    Screen::erase_line(mode);
}
void set_cursor_visible(bool visible) {
    Screen::set_cursor_visible(visible);
}

void suspend() {
    Screen::suspend();
}
void resume() {
    Screen::resume();
}

void late_init() {
    auto fb = display::surface();
    if (!fb.pixels)
        return;
    init(reinterpret_cast<uintptr_t>(fb.pixels), fb.width, fb.height, fb.stride, 32);
    if (is_active())
        klog::info("console: framebuffer %dx%d\n", fb.width, fb.height);
    else
        klog::warn("console: framebuffer initialization failed; using architecture console\n");
}

}  // namespace fbcons
