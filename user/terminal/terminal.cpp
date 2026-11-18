#include "../command.h"
#include "../desktop/fd.h"
#include "../desktop/text_frame.h"
#include <base/keyboard.h>
#include <base/terminal_parser.h>

#ifdef ZONIX_PIXEL_TERMINAL
#include "grid_renderer.h"
#endif

namespace {

using namespace desktop_ipc;

#ifndef ZONIX_PIXEL_TERMINAL
class GridRenderer {
public:
    long init() { return 0; }
    long publish(const Frame& frame) { return command_write_all(STDOUT_FD, frame.data(), sizeof(frame)); }
};
#endif

class Grid {
public:
    Grid() {
        clear();
        cursor(true);
    }
    void feed(const char* bytes, unsigned int count) {
        for (unsigned int i = 0; i < count; ++i)
            parser_.putc(static_cast<unsigned char>(bytes[i]), *this);
    }
    const Frame& frame() {
        bytes()[CURSOR_ROW] = static_cast<unsigned char>(row_);
        bytes()[CURSOR_COL] = static_cast<unsigned char>(col_);
        return frame_;
    }
    void text(int c) {
        switch (c) {
            case '\r': col_ = 0; break;
            case '\n': newline(); break;
            case '\b':
            case 127:
                if (col_ > 0)
                    --col_;
                break;
            case '\t': {
                int stop = (col_ / 8 + 1) * 8;
                for (int i = col_; i < stop; ++i)
                    text(' ');
                break;
            }
            default:
                if (c >= 32) {
                    bytes()[row_ * TERMINAL_COLS + col_] = static_cast<unsigned char>(c <= 126 ? c : '?');
                    if (++col_ == TERMINAL_COLS)
                        newline();
                }
                break;
        }
    }
    void cursor(bool visible) { bytes()[CURSOR_VISIBLE] = visible ? 1 : 0; }
    void position(unsigned int row, unsigned int col) {
        row_ = row < TERMINAL_ROWS ? row : TERMINAL_ROWS - 1;
        column(col);
    }
    void column(unsigned int col) { col_ = col < TERMINAL_COLS ? col : TERMINAL_COLS - 1; }
    void erase_line(unsigned int mode) {
        int first = mode == 2 ? 0 : col_;
        for (int i = first; i < TERMINAL_COLS; ++i)
            bytes()[row_ * TERMINAL_COLS + i] = ' ';
    }
    void clear() {
        for (int i = 0; i < TERMINAL_COLS * TERMINAL_ROWS; ++i)
            bytes()[i] = ' ';
        row_ = col_ = 0;
    }

private:
    Frame frame_{};
    terminal::Parser parser_{};
    int row_{};
    int col_{};

    unsigned char* bytes() { return reinterpret_cast<unsigned char*>(frame_.data()); }
    void newline() {
        col_ = 0;
        if (row_ < TERMINAL_ROWS - 1) {
            ++row_;
            return;
        }
        auto* cells = bytes();
        for (int i = 0; i < TERMINAL_COLS * (TERMINAL_ROWS - 1); ++i)
            cells[i] = cells[i + TERMINAL_COLS];
        erase_line(2);
    }
};

long spawn_shell(int slave) {
    Fd input(sys_fcntl(STDIN_FD, F_DUPFD_CLOEXEC, 0));
    Fd output(sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 0));
    Fd error(sys_fcntl(STDERR_FD, F_DUPFD_CLOEXEC, 0));
    if (input.get() < 0 || output.get() < 0 || error.get() < 0)
        return SYS_ERROR_FULL;
    long pid = SYS_ERROR_IO;
    if (sys_dup2(slave, STDIN_FD) >= 0 && sys_dup2(slave, STDOUT_FD) >= 0 && sys_dup2(slave, STDERR_FD) >= 0) {
        char path[] = "/bin/sh";
        char* argv[] = {path, nullptr};
        pid = sys_spawn(path, 1, argv);
    }
    long in = sys_dup2(input.get(), STDIN_FD);
    long out = sys_dup2(output.get(), STDOUT_FD);
    long err = sys_dup2(error.get(), STDERR_FD);
    return in < 0 || out < 0 || err < 0 ? SYS_ERROR_IO : pid;
}

long open_shell(TerminalSize size, int& master) {
    int ends[2];
    long result = sys_openpty(ends, &size);
    if (result < 0)
        return result;
    Fd owner(ends[0]);
    Fd slave(ends[1]);
    long pid = spawn_shell(slave.get());
    if (pid >= 0)
        master = owner.release();
    return pid;
}

void send_key(int fd, const DisplayEvent& event) {
    if (event.type != DISPLAY_EVENT_KEY || event.value == DISPLAY_KEY_RELEASE)
        return;
    int key = keyboard::KeyboardState::character(event.code, event.modifiers);
    if (key < 0)
        return;
    if (const char* seq = keyboard::sequence(key)) {
        sys_write(fd, (void*)seq, strlen((char*)seq));
    } else {
        char byte = static_cast<char>(key);
        sys_write(fd, &byte, 1);
    }
}

long run(int master) {
    Grid grid;
    unsigned int tty_bit = 1U << master;
    unsigned int input_bit = 1U << STDIN_FD;
    GridRenderer renderer;
    long error = renderer.init();
    if (error == 0)
        error = renderer.publish(grid.frame());
    while (error == 0) {
        long ready = sys_wait_readable(tty_bit | input_bit);
        if (ready < 0)
            return ready;
        if ((ready & input_bit) != 0) {
            Input input{};
            long got = sys_read(STDIN_FD, &input, sizeof(input));
            if (got == 0)
                return 0;
            if (got != sizeof(input))
                return SYS_ERROR_IO;
            send_key(master, input.event);
        }
        if ((ready & tty_bit) != 0) {
            char output[1024];
            long got = sys_read(master, output, sizeof(output));
            if (got <= 0)
                return got;
            grid.feed(output, static_cast<unsigned int>(got));
            error = renderer.publish(grid.frame());
        }
    }
    return error;
}

}  // namespace

extern "C" int main(int argc, char**) {
    if (argc != 1)
        return 2;
    int master = -1;
    long pid = open_shell({TERMINAL_ROWS, TERMINAL_COLS}, master);
    if (pid < 0)
        return command_error("terminal", "/bin/sh", pid);
    long error = run(master);
    sys_close(master);
    sys_wait(static_cast<int>(pid), nullptr);
    return error < 0 ? command_error("terminal", "PTY", error) : 0;
}
