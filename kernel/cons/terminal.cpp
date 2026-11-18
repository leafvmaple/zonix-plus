#include "terminal.h"
#include "cons/cons.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include <base/terminal_parser.h>

namespace {

class PhysicalTerminal {
public:
    static void putc(int c) {
        intr::Guard guard;
        PhysicalTerminal sink;
        parser_.putc(c, sink);
    }
    static void restore() {
        parser_.reset();
        // Use the same protocol for local display and a serial terminal.
        for (const char* c = "\x1b[?25h\x1b[2J\x1b[H"; *c; ++c)
            cons::putc(*c);
    }
    void text(int c) {
        fbcons::putc(c);  // Also buffers early output before framebuffer setup.
        if (!fbcons::is_active())
            cons::arch_text_putc(c);
    }
    void cursor(bool visible) {
        if (fbcons::is_active())
            fbcons::set_cursor_visible(visible);
        else
            cons::arch_set_cursor_visible(visible);
    }
    void position(unsigned int row, unsigned int col) {
        if (fbcons::is_active())
            fbcons::move_cursor(row, col);
        else
            cons::arch_move_cursor(row, col);
    }
    void column(unsigned int col) {
        if (fbcons::is_active())
            fbcons::move_column(col);
        else
            cons::arch_move_column(col);
    }
    void clear() {
        if (fbcons::is_active())
            fbcons::clear();
        else
            cons::arch_clear_screen();
    }
    void erase_line(unsigned int mode) {
        if (fbcons::is_active())
            fbcons::erase_line(mode);
        else
            cons::arch_erase_line(mode);
    }

private:
    inline static terminal::Parser parser_{};
};

}  // namespace

namespace terminal {
void putc(int c) {
    PhysicalTerminal::putc(c);
}
TerminalSize size() {
    return fbcons::is_active() ? fbcons::size() : cons::arch_screen_size();
}
void restore() {
    PhysicalTerminal::restore();
}
}  // namespace terminal
