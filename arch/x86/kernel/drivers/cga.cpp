#include "drivers/cga.h"
#include <asm/arch.h>
#include <asm/memlayout.h>
#include "lib/cons_defs.h"
#include "drivers/intr.h"

namespace cga {
namespace {
class Screen {
public:
    static int init() {
        arch_port_write8(INDEX_REG, CURSOR_HIGH);
        position_ = arch_port_read8(DATA_REG) << 8;
        arch_port_write8(INDEX_REG, CURSOR_LOW);
        position_ |= arch_port_read8(DATA_REG);
        if (position_ >= console::ROWS * console::COLS)
            position_ = 0;
        return 0;
    }
    static void putc(int c) {
        intr::Guard guard;
        switch (c & 0xFF) {
            case '\b':
                if (position_ > 0)
                    buffer()[--position_] = ERASE_CHAR;
                break;
            case '\n': position_ += console::COLS; [[fallthrough]];
            case '\r': position_ -= position_ % console::COLS; break;
            case '\t': position_ += 8 - position_ % 8; break;
            default: buffer()[position_++] = static_cast<uint16_t>(0x0700 | (c & 0xFF)); break;
        }
        if (position_ >= console::ROWS * console::COLS)
            scroll_up();
        update_cursor();
    }
    static void scroll_up() {
        auto* cells = buffer();
        for (int i = 0; i < (console::ROWS - 1) * console::COLS; ++i)
            cells[i] = cells[i + console::COLS];
        for (int i = (console::ROWS - 1) * console::COLS; i < console::ROWS * console::COLS; ++i)
            cells[i] = ERASE_CHAR;
        position_ -= console::COLS;
    }
    static void move_cursor(unsigned int row, unsigned int col) {
        intr::Guard guard;
        if (row >= console::ROWS)
            row = console::ROWS - 1;
        if (col >= console::COLS)
            col = console::COLS - 1;
        position_ = static_cast<uint16_t>(row * console::COLS + col);
        update_cursor();
    }
    static void clear() {
        intr::Guard guard;
        for (int i = 0; i < console::ROWS * console::COLS; ++i)
            buffer()[i] = ERASE_CHAR;
    }
    static void erase_line(unsigned int mode) {
        intr::Guard guard;
        unsigned int start = mode == 2 ? position_ - position_ % console::COLS : position_;
        unsigned int end = (position_ / console::COLS + 1) * console::COLS;
        for (unsigned int i = start; i < end; ++i)
            buffer()[i] = ERASE_CHAR;
    }
    static void set_cursor_visible(bool visible) {
        intr::Guard guard;
        arch_port_write8(INDEX_REG, CURSOR_START);
        uint8_t start = arch_port_read8(DATA_REG);
        arch_port_write8(DATA_REG, visible ? start & ~CURSOR_DISABLE : start | CURSOR_DISABLE);
    }

private:
    static constexpr uint16_t INDEX_REG = 0x3D4;
    static constexpr uint16_t DATA_REG = 0x3D5;
    static constexpr uint8_t CURSOR_HIGH = 0x0E;
    static constexpr uint8_t CURSOR_LOW = 0x0F;
    static constexpr uint8_t CURSOR_START = 0x0A;
    static constexpr uint8_t CURSOR_DISABLE = 0x20;
    static constexpr uint16_t ERASE_CHAR = 0x0720;
    static constexpr uintptr_t BUFFER_PA = 0xB8000;
    inline static uint16_t position_{};
    static uint16_t* buffer() { return reinterpret_cast<uint16_t*>(BUFFER_PA + KERNEL_BASE); }
    static void update_cursor() {
        arch_port_write8(INDEX_REG, CURSOR_HIGH);
        arch_port_write8(DATA_REG, position_ >> 8);
        arch_port_write8(INDEX_REG, CURSOR_LOW);
        arch_port_write8(DATA_REG, position_);
    }
};
}  // namespace
int init() {
    return Screen::init();
}
void putc(int c) {
    Screen::putc(c);
}
void scrup() {
    Screen::scroll_up();
}
TerminalSize size() {
    return {console::ROWS, console::COLS};
}
void move_cursor(unsigned int row, unsigned int col) {
    Screen::move_cursor(row, col);
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
}  // namespace cga
