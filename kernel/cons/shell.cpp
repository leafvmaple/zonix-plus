#include "shell.h"
#include "cons.h"
#include "cmd/cmd.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/array.h"
#include "lib/cons_defs.h"

namespace {

constexpr size_t CMD_BUF_SIZE = 128;
constexpr int MAX_ARGS = 16;
constexpr int MAX_COMMANDS = 64;

char cmd_buffer[CMD_BUF_SIZE];
size_t cmd_pos = 0;

struct ShellCommand {
    const char* name{};
    const char* desc{};
    shell::fnCommand func{};
};

Array<ShellCommand, MAX_COMMANDS> commands{};

static int parse_args(const char* cmd, char** argv) {
    static char arg_buf[CMD_BUF_SIZE];
    int argc = 0;

    size_t i = 0;
    while (cmd[i] && i < CMD_BUF_SIZE - 1) {
        arg_buf[i] = cmd[i];
        i++;
    }
    arg_buf[i] = '\0';

    char* p = arg_buf;
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

static void execute_command(const char* cmd) {
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

    for (ShellCommand& entry : commands) {
        if (strcmp(argv[0], entry.name) == 0) {
            entry.func(argc, argv);
            return;
        }
    }

    cprintf("Unknown command: %s\n", argv[0]);
    cprintf("Type 'help' for available commands.\n");
}

}  // namespace

int shell::register_command(const char* name, const char* desc, fnCommand func) {
    if (name == nullptr || desc == nullptr || func == nullptr) {
        return -1;
    }

    for (const ShellCommand& entry : commands) {
        if (strcmp(name, entry.name) == 0) {
            return -1;
        }
    }

    if (!commands.push_back({name, desc, func})) {
        return -1;
    }
    return 0;
}

void shell::print_commands() {
    cprintf("Available commands:\n");
    for (const ShellCommand& entry : commands) {
        cprintf("  %-10s - %s\n", entry.name, entry.desc);
    }
}

[[gnu::weak]] extern void shell_register_extensions();

void shell::prompt() {
    cprintf("zonix> ");
}

void shell::init() {
    cmd_pos = 0;
    cmd_buffer[0] = '\0';
    commands.clear();

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
            cmd_buffer[cmd_pos] = '\0';
            execute_command(cmd_buffer);
            cmd_pos = 0;
            shell::prompt();
            break;

        case '\b':
            if (cmd_pos > 0) {
                cmd_pos--;
                cons::putc('\b');
            }
            break;

        case ASCII_DEL: break;

        default:
            if (cmd_pos < CMD_BUF_SIZE - 1 && c >= ASCII_PRINTABLE_MIN && c < ASCII_PRINTABLE_MAX) {
                cmd_buffer[cmd_pos++] = c;
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
