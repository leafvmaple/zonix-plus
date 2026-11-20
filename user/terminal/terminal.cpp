#include "../command.h"
#include "../desktop/fd.h"
#include "../desktop/text_frame.h"
#include <base/keyboard.h>
#include "grid.h"
#include "grid_renderer.h"

namespace {

using namespace desktop_ipc;

using terminal_grid::Grid;

// The largest grid a window can show.
constexpr int MAX_COLS = static_cast<int>(PIXEL_MAX_WIDTH) / GLYPH_WIDTH;
constexpr int MAX_ROWS = static_cast<int>(PIXEL_MAX_HEIGHT) / GLYPH_HEIGHT;

long spawn_session(int slave, int argc, char** argv) {
    Fd input(sys_fcntl(STDIN_FD, F_DUPFD_CLOEXEC, 0));
    Fd output(sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 0));
    Fd error(sys_fcntl(STDERR_FD, F_DUPFD_CLOEXEC, 0));
    if (input.get() < 0 || output.get() < 0 || error.get() < 0)
        return SYS_ERROR_FULL;
    long pid = SYS_ERROR_IO;
    if (sys_dup2(slave, STDIN_FD) >= 0 && sys_dup2(slave, STDOUT_FD) >= 0 && sys_dup2(slave, STDERR_FD) >= 0)
        pid = sys_spawn(argv[0], argc, argv);
    long in = sys_dup2(input.get(), STDIN_FD);
    long out = sys_dup2(output.get(), STDOUT_FD);
    long err = sys_dup2(error.get(), STDERR_FD);
    return in < 0 || out < 0 || err < 0 ? SYS_ERROR_IO : pid;
}

long open_session(TerminalSize size, int argc, char** argv, int& master) {
    int ends[2];
    long result = sys_openpty(ends, &size);
    if (result < 0)
        return result;
    Fd owner(ends[0]);
    Fd slave(ends[1]);
    long pid = spawn_session(slave.get(), argc, argv);
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

// Fits the grid to a window size Desktop proposes, in whole cells, and tells
// the shell's terminal the new size.
long resize(Grid& grid, GridRenderer& renderer, int master, int width, int height) {
    int cols = sys::max(Grid::MIN_COLS, sys::min(width / GLYPH_WIDTH, MAX_COLS));
    int rows = sys::max(Grid::MIN_ROWS, sys::min(height / GLYPH_HEIGHT, MAX_ROWS));
    if (cols == grid.cols() && rows == grid.rows())
        return 0;
    grid.resize(cols, rows);
    TerminalSize size{static_cast<unsigned int>(rows), static_cast<unsigned int>(cols)};
    sys_ioctl(master, TERMINAL_SET_SIZE, reinterpret_cast<long>(&size));
    long error = renderer.resize(cols, rows);
    return error < 0 ? error : renderer.publish(grid);
}

long run(int master) {
    Grid grid(TERMINAL_COLS, TERMINAL_ROWS);
    unsigned int tty_bit = 1U << master;
    unsigned int input_bit = 1U << STDIN_FD;
    GridRenderer renderer;
    long error = renderer.resize(grid.cols(), grid.rows());
    if (error == 0)
        error = renderer.publish(grid);
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
            if (input.event.type == INPUT_RESIZE)
                error = resize(grid, renderer, master, input.x, input.y);
            else
                send_key(master, input.event);
        }
        if (error == 0 && (ready & tty_bit) != 0) {
            char output[1024];
            long got = sys_read(master, output, sizeof(output));
            if (got <= 0)
                return got;
            grid.feed(output, static_cast<unsigned int>(got));
            error = renderer.publish(grid);
        }
    }
    return error;
}

}  // namespace

extern "C" int main(int argc, char**) {
    if (argc != 1)
        return command_usage("Usage: terminal\n");
    char shell[] = "/bin/sh";
    char* args[]{shell, nullptr};
    int master = -1;
    long pid = open_session({TERMINAL_ROWS, TERMINAL_COLS}, 1, args, master);
    if (pid < 0)
        return command_error("terminal", shell, pid);
    long error = run(master);
    sys_close(master);
    sys_wait(static_cast<int>(pid), nullptr);
    return error < 0 ? command_error("terminal", "PTY", error) : 0;
}
