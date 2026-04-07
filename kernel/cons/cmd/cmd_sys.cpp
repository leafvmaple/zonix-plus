#include "cmd.h"

#include "cons/cons.h"
#include "exec/exec.h"
#include "fs/vfs.h"
#include "lib/result.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include "sched/sched.h"

#include <kernel/sysinfo.h>
#include "lib/cons_defs.h"

namespace cmd {

static void cmd_help(int argc, char** argv);

static void cmd_pgdir(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    vmm::print_pgdir();
}

static void cmd_clear(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    for (int i = 0; i < SCREEN_ROWS; i++) {
        cprintf("\n");
    }
}

static void cmd_uname(int argc, char** argv) {
    int show_all = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
            break;
        }
    }

    if (show_all) {
        cprintf("%s %s %s %s %s\n", SYSINFO_NAME, SYSINFO_HOSTNAME, ZONIX_VERSION_STRING, SYSINFO_VERSION,
                SYSINFO_MACHINE);
    } else {
        cprintf("%s\n", SYSINFO_NAME);
    }
}

static void cmd_ps(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    sched::print();
}

static void cmd_schedstat(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    sched::print_stats();
}

static void cmd_exec(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: exec <filename> [/mnt]\n");
        cprintf("  exec <filename>      - run ELF from system disk (/)\n");
        cprintf("  exec <filename> /mnt - run ELF from mounted disk (/mnt)\n");
        return;
    }

    const char* filename = argv[1];

    int use_mnt = 0;
    if (argc >= 3 && strcmp(argv[2], "/mnt") == 0) {
        use_mnt = 1;
    }

    if (use_mnt) {
        if (!mnt_mounted()) {
            cprintf("Nothing mounted at /mnt\n");
            return;
        }
    } else {
        if (!vfs::is_mounted("/")) {
            cprintf("Error: no system disk mounted at /\n");
            return;
        }
    }

    char path_buf[PATH_BUF_SIZE]{};
    if (build_path(filename, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long: %s\n", filename);
        return;
    }

    auto pid_r = exec::exec(path_buf);
    if (pid_r.ok()) {
        cprintf("Process started (PID %d)\n", pid_r.value());
        int exit_code = 0;
        (void)sched::wait(pid_r.value(), &exit_code);
    } else {
        cprintf("Failed to execute: %s\n", filename);
    }
}

static void cmd_help(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    shell::print_commands();
}

void register_sys_commands() {
    shell::register_command("help", "Show this help message", cmd_help);
    shell::register_command("pgdir", "Print page directory", cmd_pgdir);
    shell::register_command("clear", "Clear the screen", cmd_clear);
    shell::register_command("uname", "Print system information (-a for all)", cmd_uname);
    shell::register_command("ps", "List all processes", cmd_ps);
    shell::register_command("schedstat", "Show scheduler statistics", cmd_schedstat);
    shell::register_command("exec", "Run ELF binary (usage: exec <file> [/mnt])", cmd_exec);
}

}  // namespace cmd
