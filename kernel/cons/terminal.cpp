#include "terminal.h"
#include "cons/cons.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include <sys/array.hpp>

namespace {
class Terminal {
public:
    static void putc(int c) {
        intr::Guard guard;
        if (c == 27) {
            state_ = State::Escape;
            params_ = {};
            index_ = 0;
            private_ = false;
            return;
        }
        if (state_ == State::Escape) {
            state_ = c == '[' ? State::Csi : State::Text;
            return;
        }
        if (state_ == State::Csi) {
            if (c == '?' && index_ == 0 && params_[0] == 0) {
                private_ = true;
                return;
            }
            if (c >= '0' && c <= '9') {
                if (params_[index_] < 10000)
                    params_[index_] = params_[index_] * 10 + static_cast<unsigned int>(c - '0');
                return;
            }
            if (c == ';' && index_ + 1 < params_.size()) {
                ++index_;
                return;
            }
            if (c >= 0x40 && c <= 0x7E) {
                execute(c);
                state_ = State::Text;
            } else if (c != ';') {
                state_ = State::Text;
            }
            return;
        }
        fbcons::putc(c);  // Also buffers early output before framebuffer setup.
        if (!fbcons::is_active())
            cons::arch_text_putc(c);
    }
    static TerminalSize size() { return fbcons::is_active() ? fbcons::size() : cons::arch_screen_size(); }
    static void restore() {
        state_ = State::Text;
        // Use the same protocol for local display and a serial terminal.
        for (const char* c = "\x1b[?25h\x1b[2J\x1b[H"; *c; ++c)
            cons::putc(*c);
    }

private:
    enum class State { Text, Escape, Csi };
    inline static State state_{};
    inline static sys::array<unsigned int, 2> params_{};
    inline static size_t index_{};
    inline static bool private_{};

    static void execute(int command) {
        bool framebuffer = fbcons::is_active();
        if (private_) {
            if (params_[0] == 25 && (command == 'h' || command == 'l')) {
                if (framebuffer)
                    fbcons::set_cursor_visible(command == 'h');
                else
                    cons::arch_set_cursor_visible(command == 'h');
            }
            return;
        }
        if (command == 'H' || command == 'f') {
            unsigned int row = params_[0] ? params_[0] - 1 : 0;
            unsigned int col = params_[1] ? params_[1] - 1 : 0;
            if (framebuffer)
                fbcons::move_cursor(row, col);
            else
                cons::arch_move_cursor(row, col);
        } else if (command == 'G') {
            unsigned int col = params_[0] ? params_[0] - 1 : 0;
            if (framebuffer)
                fbcons::move_column(col);
            else
                cons::arch_move_column(col);
        } else if (command == 'J' && params_[0] == 2) {
            if (framebuffer)
                fbcons::clear();
            else
                cons::arch_clear_screen();
        } else if (command == 'K' && (params_[0] == 0 || params_[0] == 2)) {
            if (framebuffer)
                fbcons::erase_line(params_[0]);
            else
                cons::arch_erase_line(params_[0]);
        }
    }
};
}  // namespace

namespace terminal {
void putc(int c) {
    Terminal::putc(c);
}
TerminalSize size() {
    return Terminal::size();
}
void restore() {
    Terminal::restore();
}
}  // namespace terminal
