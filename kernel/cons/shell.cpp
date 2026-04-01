#include "shell.h"
#include "cons.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "mm/vmm.h"
#include "block/blk.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "exec/exec.h"

#include <base/bpb.h>
#include <base/types.h>
#include <kernel/sysinfo.h>
#include "lib/array.h"
#include "lib/cons_defs.h"

// Command buffer configuration
namespace {

constexpr size_t CMD_BUF_SIZE = 128;
constexpr int MAX_ARGS = 16;
constexpr int MAX_COMMANDS = 64;

char cmd_buffer[CMD_BUF_SIZE];
size_t cmd_pos = 0;

}  // namespace

struct ShellCommand {
    const char* name{};
    const char* desc{};
    shell::fnCommand func{};
};

namespace {

Array<ShellCommand, MAX_COMMANDS> commands{};

}  // namespace

static int strcmp(const char* s1, const char* s2);
static size_t strlen(const char* s);
static int parse_args(const char* cmd, char** argv);

static void cmd_help(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    cprintf("Available commands:\n");
    for (const ShellCommand& cmd : commands) {
        cprintf("  %-10s - %s\n", cmd.name, cmd.desc);
    }
}

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

static void cmd_lsblk(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    BlockManager::print();
}

static void cmd_hdparm(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    int count = BlockManager::get_device_count();
    if (count == 0) {
        cprintf("No disk devices found\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk)
            continue;

        dev->print_info();
    }
}

static void cmd_dd(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    cprintf("dd - disk read/write utility\n");
    cprintf("Note: Full dd command with parameters not yet implemented\n");
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
        // uname -a: print all information
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

static const char* g_mnt_device{};
static int g_mnt_mounted{};

static void cmd_mount(int argc, char** argv) {
    if (g_mnt_mounted || vfs::is_mounted("/mnt")) {
        const char* mounted = g_mnt_device ? g_mnt_device : vfs::mounted_device("/mnt");
        cprintf("Device already mounted at /mnt: %s\n", mounted ? mounted : "(unknown)");
        cprintf("Use 'umount' to unmount first\n");
        return;
    }

    if (argc < 2) {
        cprintf("Usage: mount <device>\n");
        cprintf("Example: mount hdb\n");
        return;
    }

    const char* dev_name = argv[1];

    const char* root_dev = vfs::mounted_device("/");
    if (root_dev && strcmp(dev_name, root_dev) == 0) {
        cprintf("Error: %s is the system disk and already mounted at /\n", dev_name);
        return;
    }

    BlockDevice* dev = BlockManager::get_device(dev_name);
    if (!dev) {
        cprintf("Device not found: %s\n", dev_name);
        cprintf("Use 'lsblk' to see available devices\n");
        return;
    }

    cprintf("Mounting %s at /mnt...\n", dev->name);

    if (vfs::mount("/mnt", dev, "fat") == 0) {
        g_mnt_device = dev->name;
        g_mnt_mounted = 1;
        cprintf("Successfully mounted %s at /mnt\n", dev->name);
    } else {
        cprintf("Failed to mount file system\n");
        cprintf("Make sure the device contains a valid FAT12/FAT16/FAT32 file system\n");
    }
}

static void cmd_umount(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    if (!g_mnt_mounted || !vfs::is_mounted("/mnt")) {
        cprintf("Nothing mounted at /mnt\n");
        g_mnt_mounted = 0;
        g_mnt_device = nullptr;
        return;
    }

    if (vfs::umount("/mnt") != 0) {
        cprintf("Failed to unmount /mnt\n");
        return;
    }

    cprintf("Unmounting %s from /mnt...\n", g_mnt_device);
    g_mnt_device = nullptr;
    g_mnt_mounted = 0;
    cprintf("Successfully unmounted /mnt\n");
}

static void cmd_info(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    if (!vfs::is_mounted("/")) {
        cprintf("Error: no system disk mounted at /\n");
        return;
    }

    cprintf("System Disk Information:\n");
    const char* root_dev = vfs::mounted_device("/");
    if (root_dev) {
        cprintf("  Device: %s\n", root_dev);
    }
    cprintf("  Mount Point: /\n");
    vfs::print_mount_info("/");

    if (g_mnt_mounted) {
        cprintf("\n/mnt Information:\n");
        cprintf("  Device: %s\n", g_mnt_device);
        cprintf("  Mount Point: /mnt\n");
        vfs::print_mount_info("/mnt");
    }
}

static int ls_callback(const vfs::DirEntry& entry, void* arg) {
    static_cast<void>(arg);

    // Get file attributes
    char attr_str[6] = "-----";
    if (entry.attrs & FAT_ATTR_DIRECTORY)
        attr_str[0] = 'd';
    if (entry.attrs & FAT_ATTR_READ_ONLY)
        attr_str[1] = 'r';
    if (entry.attrs & FAT_ATTR_HIDDEN)
        attr_str[2] = 'h';
    if (entry.attrs & FAT_ATTR_SYSTEM)
        attr_str[3] = 's';
    if (entry.attrs & FAT_ATTR_ARCHIVE)
        attr_str[4] = 'a';

    uint32_t size = entry.size;

    cprintf("%s %8d  %s\n", attr_str, size, entry.name);

    return 0;  // Continue
}

static void cmd_ls(int argc, char** argv) {
    // Check if path is /mnt
    int use_mnt = 0;
    if (argc >= 2 && strcmp(argv[1], "/mnt") == 0) {
        use_mnt = 1;
    }

    const char* path = nullptr;

    if (use_mnt) {
        if (!g_mnt_mounted) {
            cprintf("Nothing mounted at /mnt\n");
            cprintf("Use 'mount <device>' to mount a device\n");
            return;
        }
        path = "/mnt";
    } else {
        if (!vfs::is_mounted("/")) {
            cprintf("Error: no system disk mounted at /\n");
            return;
        }
        path = "/";
    }

    cprintf("Directory listing of %s:\n", path);
    cprintf("ATTR     SIZE     NAME\n");
    cprintf("-------- -------- ------------\n");

    int count = vfs::readdir(path, ls_callback, nullptr);

    if (count < 0) {
        cprintf("Failed to read directory\n");
    } else {
        cprintf("\nTotal: %d file(s)\n", count);
    }
}

static int build_path(const char* filename, int use_mnt, char* out, size_t out_size) {
    if (!filename || !out || out_size == 0) {
        return -1;
    }

    const char* prefix = use_mnt ? "/mnt/" : "";
    size_t prefix_len = strlen(prefix);
    size_t file_len = strlen(filename);

    if (prefix_len + file_len + 1 > out_size) {
        return -1;
    }

    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, filename, file_len + 1);

    return 0;
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: cat <filename> [/mnt]\n");
        cprintf("  cat <filename>     - read file from system disk (/)\n");
        cprintf("  cat <filename> /mnt - read file from mounted disk (/mnt)\n");
        return;
    }

    const char* filename = argv[1];

    int use_mnt = 0;
    if (argc >= 3 && strcmp(argv[2], "/mnt") == 0) {
        use_mnt = 1;
    }

    if (use_mnt) {
        if (!g_mnt_mounted) {
            cprintf("Nothing mounted at /mnt\n");
            return;
        }
    } else {
        if (!vfs::is_mounted("/")) {
            cprintf("Error: no system disk mounted at /\n");
            return;
        }
    }

    char path_buf[CMD_BUF_SIZE]{};
    if (build_path(filename, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long: %s\n", filename);
        return;
    }

    vfs::File* file = nullptr;
    if (vfs::open(path_buf, &file) != 0 || !file) {
        cprintf("File not found: %s\n", filename);
        return;
    }

    vfs::Stat st{};
    if (file->stat(&st) != 0) {
        vfs::close(file);
        cprintf("Failed to stat file: %s\n", filename);
        return;
    }

    if (st.type != vfs::NodeType::File) {
        vfs::close(file);
        cprintf("Cannot cat a directory\n");
        return;
    }

    // Allocate buffer for file contents (max 64KB for now)
    uint32_t max_size = 65536;
    uint32_t size = st.size;
    if (size > max_size) {
        cprintf("File too large (max %d bytes)\n", max_size);
        size = max_size;
    }

    if (size == 0) {
        vfs::close(file);
        cprintf("(empty file)\n");
        return;
    }

    // Use static buffer to avoid allocation
    static uint8_t file_buf[4096];
    uint32_t offset = 0;

    cprintf("--- File: %s (%d bytes) ---\n", filename, st.size);

    while (offset < size) {
        uint32_t chunk_size = size - offset;
        if (chunk_size > sizeof(file_buf)) {
            chunk_size = sizeof(file_buf);
        }

        int read = vfs::read(file, file_buf, chunk_size, offset);
        if (read <= 0) {
            cprintf("\nError reading file at offset %d\n", offset);
            break;
        }

        // Print contents
        for (int i = 0; i < read; i++) {
            char c = file_buf[i];
            if (c == '\n') {
                cons::putc('\n');
            } else if (c == '\r') {
                // Skip CR
            } else if (c >= 32 && c < 127) {
                cons::putc(c);
            } else {
                cons::putc('.');  // Non-printable
            }
        }

        offset += read;
    }

    vfs::close(file);

    cprintf("\n--- End of file ---\n");
}

static void cmd_exec(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: exec <filename> [/mnt]\n");
        cprintf("  exec <filename>      - run ELF from system disk (/)\n");
        cprintf("  exec <filename> /mnt - run ELF from mounted disk (/mnt)\n");
        return;
    }

    const char* filename = argv[1];

    // Check if loading from /mnt
    int use_mnt = 0;
    if (argc >= 3 && strcmp(argv[2], "/mnt") == 0) {
        use_mnt = 1;
    }

    if (use_mnt) {
        if (!g_mnt_mounted) {
            cprintf("Nothing mounted at /mnt\n");
            return;
        }
    } else {
        if (!vfs::is_mounted("/")) {
            cprintf("Error: no system disk mounted at /\n");
            return;
        }
    }

    char path_buf[CMD_BUF_SIZE]{};
    if (build_path(filename, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long: %s\n", filename);
        return;
    }

    int pid = exec::exec(path_buf);
    if (pid > 0) {
        cprintf("Process started (PID %d)\n", pid);
        // Wait for the child process to exit before returning to the shell prompt
        int exit_code = 0;
        sched::wait(pid, &exit_code);
    } else {
        cprintf("Failed to execute: %s\n", filename);
    }
}

[[gnu::weak]] extern void shell_register_extensions();

static void register_builtin_command(const char* name, const char* desc, shell::fnCommand func) {
    if (shell::register_command(name, desc, func) != 0) {
        cprintf("shell: failed to register command '%s'\n", name);
    }
}

static void register_builtin_commands() {
    register_builtin_command("help", "Show this help message", cmd_help);
    register_builtin_command("pgdir", "Print page directory", cmd_pgdir);
    register_builtin_command("clear", "Clear the screen", cmd_clear);
    register_builtin_command("lsblk", "List block devices", cmd_lsblk);
    register_builtin_command("hdparm", "Show disk information", cmd_hdparm);
    register_builtin_command("dd", "Disk dump/copy (info only)", cmd_dd);
    register_builtin_command("uname", "Print system information (-a for all)", cmd_uname);
    register_builtin_command("ps", "List all processes", cmd_ps);
    register_builtin_command("schedstat", "Show scheduler statistics", cmd_schedstat);
    register_builtin_command("mount", "Mount device to /mnt (usage: mount <device>)", cmd_mount);
    register_builtin_command("umount", "Unmount /mnt", cmd_umount);
    register_builtin_command("info", "Show file system information", cmd_info);
    register_builtin_command("ls", "List files (usage: ls [/mnt])", cmd_ls);
    register_builtin_command("cat", "Display file contents (usage: cat <file> [/mnt])", cmd_cat);
    register_builtin_command("exec", "Run ELF binary (usage: exec <file> [/mnt])", cmd_exec);
}

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

    for (ShellCommand& cmd : commands) {
        if (strcmp(argv[0], cmd.name) == 0) {
            cmd.func(argc, argv);
            return;
        }
    }

    cprintf("Unknown command: %s\n", argv[0]);
    cprintf("Type 'help' for available commands.\n");
}

int shell::register_command(const char* name, const char* desc, fnCommand func) {
    if (name == nullptr || desc == nullptr || func == nullptr) {
        return -1;
    }

    for (const ShellCommand& cmd : commands) {
        if (strcmp(name, cmd.name) == 0) {
            return -1;
        }
    }

    ShellCommand cmd{};
    cmd.name = name;
    cmd.desc = desc;
    cmd.func = func;
    if (!commands.push_back({name, desc, func})) {
        return -1;
    }
    return 0;
}

void shell::prompt() {
    cprintf("zonix> ");
}

void shell::init() {
    cmd_pos = 0;
    cmd_buffer[0] = '\0';
    commands.clear();
    register_builtin_commands();
    if (shell_register_extensions) {
        shell_register_extensions();
    }

    cprintf("\n");
    cprintf("=============================================\n");
    cprintf("  Welcome to Zonix OS Interactive Console\n");
    cprintf("  Type 'help' to see available commands\n");
    cprintf("=============================================\n");
    // Don't print prompt yet - wait until system is fully ready
}

void shell::handle_char(char c) {
    if (c <= 0) {
        return;  // Invalid character
    }

    switch (c) {
        case '\n':
        case '\r':
            // Execute command
            cons::putc('\n');
            cmd_buffer[cmd_pos] = '\0';
            execute_command(cmd_buffer);
            cmd_pos = 0;
            shell::prompt();
            break;

        case '\b':
            // Backspace
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

    return 0;  // Never reached
}
