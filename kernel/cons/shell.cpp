#include "shell.h"
#include "cons.h"
#include "cmd/cmd.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include <sys/inplace_vector.hpp>
#include "lib/cons_defs.h"

namespace {

constexpr size_t CMD_BUF_SIZE = 128;
constexpr int MAX_ARGS = 16;
constexpr int MAX_COMMANDS = 64;

struct ShellCommand {
    const char* name{};
    const char* desc{};
    shell::CommandCallback func{};
};

class State {
    friend int shell::register_command(const char*, const char*, shell::CommandCallback);
    friend void shell::print_commands();
    friend void shell::init();
    friend void shell::handle_char(char);

public:
    static int parse_args(const char* cmd, char** argv);
    static void execute_command(const char* cmd);

private:
    inline static char cmd_buffer_[CMD_BUF_SIZE]{};
    inline static size_t cmd_pos_{};
    inline static char arg_buf_[CMD_BUF_SIZE]{};
    inline static sys::inplace_vector<ShellCommand, MAX_COMMANDS> commands_{};
};

int State::parse_args(const char* cmd, char** argv) {
    int argc = 0;

    size_t i = 0;
    while (cmd[i] && i < CMD_BUF_SIZE - 1) {
        State::arg_buf_[i] = cmd[i];
        i++;
    }
    State::arg_buf_[i] = '\0';

    char* p = State::arg_buf_;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        argv[argc++] = p;

        while (*p && *p != ' ') {
            p++;
        }

        if (*p) {
            *p = '\0';
            p++;
        }
    }

    return argc;
}

void State::execute_command(const char* cmd) {
    char* argv[MAX_ARGS];

    while (*cmd == ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        return;
    }

    int argc = parse_args(cmd, argv);
    if (argc == 0) {
        return;
    }

    for (ShellCommand& entry : State::commands_) {
        if (strcmp(argv[0], entry.name) == 0) {
            entry.func(argc, argv);
            return;
        }
    }

    cprintf("Unknown command: %s\n", argv[0]);
    cprintf("Type 'help' for available commands.\n");
}

}  // namespace

int shell::register_command(const char* name, const char* desc, CommandCallback func) {
    if (name == nullptr || desc == nullptr || func == nullptr) {
        return -1;
    }

    for (const ShellCommand& entry : State::commands_) {
        if (strcmp(name, entry.name) == 0) {
            return -1;
        }
    }

    if (!State::commands_.try_push_back(ShellCommand{name, desc, func})) {
        return -1;
    }
    return 0;
}

void shell::print_commands() {
    cprintf("Available commands:\n");
    for (const ShellCommand& entry : State::commands_) {
        cprintf("  %-10s - %s\n", entry.name, entry.desc);
    }
}

[[gnu::weak]] extern void shell_register_extensions();

void shell::prompt() {
    cprintf("zonix> ");
}

void shell::init() {
    State::cmd_pos_ = 0;
    State::cmd_buffer_[0] = '\0';
    State::commands_.clear();

    cmd::register_sys_commands();
    cmd::register_blk_commands();
    cmd::register_fs_commands();

    if (shell_register_extensions) {
        shell_register_extensions();
    }

    cprintf("\n");
    cprintf("=============================================\n");
    cprintf("  Welcome to Zonix OS Interactive Console\n");
    cprintf("  Type 'help' to see available commands\n");
    cprintf("=============================================\n");
}

void shell::handle_char(char c) {
    if (c <= 0) {
        return;
    }

    switch (c) {
        case '\n':
        case '\r':
            cons::putc('\n');
            State::cmd_buffer_[State::cmd_pos_] = '\0';
            State::execute_command(State::cmd_buffer_);
            State::cmd_pos_ = 0;
            shell::prompt();
            break;

        case '\b':
            if (State::cmd_pos_ > 0) {
                State::cmd_pos_--;
                cons::putc('\b');
            }
            break;

        case ASCII_DEL: break;

        default:
            if (State::cmd_pos_ < CMD_BUF_SIZE - 1 && c >= ASCII_PRINTABLE_MIN && c < ASCII_PRINTABLE_MAX) {
                State::cmd_buffer_[State::cmd_pos_++] = c;
                cons::putc(c);
            }
            break;
    }
}

int shell::main(void* arg) {
    static_cast<void>(arg);

    shell::init();
    shell::prompt();

    while (true) {
        char c = cons::getc();
        if (c > 0) {
            shell::handle_char(c);
        }
    }

    return 0;
}
