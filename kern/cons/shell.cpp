#include "shell.h"
#include "cons.h"
#include "stdio.h"
#include "../mm/vmm.h"
#include "../mm/swap_test.h"
#include "../drivers/hd.h"
#include "../drivers/blk.h"
#include "../sched/sched.h"
#include "../fs/fat.h"

#include <base/types.h>
#include <kernel/sysinfo.h>
#include "cons_defs.h"

// Command buffer configuration
#define CMD_BUF_SIZE 128
#define MAX_ARGS 16

static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_pos = 0;

typedef struct {
    const char *name;
    const char *desc;
    void (*func)(int argc, char **argv);
} shell_cmd_t;

// Forward declarations
static int strncmp(const char *s1, const char *s2, size_t n);
static int strcmp(const char *s1, const char *s2);
static int parse_args(const char *cmd, char **argv);

// Command implementations
static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    extern shell_cmd_t commands[];
    extern int command_count;
    
    cprintf("Available commands:\n");
    for (int i = 0; i < command_count; i++) {
        cprintf("  %-10s - %s\n", commands[i].name, commands[i].desc);
    }
}

static void cmd_pgdir(int argc, char **argv) {
    (void)argc; (void)argv;
    print_pgdir();
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    // Simple clear by printing newlines
    for (int i = 0; i < SCREEN_ROWS; i++) {
        cprintf("\n");
    }
}

static void cmd_swap_test(int argc, char **argv) {
    (void)argc; (void)argv;
    run_swap_tests();
}

static void cmd_lsblk(int argc, char **argv) {
    (void)argc; (void)argv;
    blk_list_devices();
}

static void cmd_hdparm(int argc, char **argv) {
    (void)argc; (void)argv;
    int num_devices = hd_get_device_count();
    
    if (num_devices == 0) {
        cprintf("No disk devices found\n");
        return;
    }
    
    cprintf("IDE Disk Information (%d device(s) found):\n\n", num_devices);
    
    for (int dev_id = 0; dev_id < 4; dev_id++) {
        ide_device_t *dev = hd_get_device(dev_id);
        
        if (dev == nullptr) {
            continue;
        }
        
        cprintf("Device: %s (dev_id=%d)\n", dev->name, dev_id);
        cprintf("  Channel: %s, Drive: %s\n",
               dev->channel == 0 ? "Primary" : "Secondary",
               dev->drive == 0 ? "Master" : "Slave");
        cprintf("  Base I/O: 0x%x, IRQ: %d\n", dev->base, dev->irq);
        cprintf("  Size: %d sectors (%d MB)\n", 
               dev->info.size, dev->info.size / 2048);
        cprintf("  CHS: %d cylinders, %d heads, %d sectors/track\n", 
               dev->info.cylinders, dev->info.heads, dev->info.sectors);
        cprintf("\n");
    }
}

static void cmd_disktest(int argc, char **argv) {
    (void)argc; (void)argv;
    cprintf("Running disk test...\n");
    hd_test();
}

static void cmd_intrtest(int argc, char **argv) {
    (void)argc; (void)argv;
    cprintf("Running interrupt test...\n");
    hd_test_interrupt();
}

static void cmd_dd(int argc, char **argv) {
    (void)argc; (void)argv;
    cprintf("dd - disk read/write utility\n");
    cprintf("Usage: Use disktest for basic disk I/O testing\n");
    cprintf("Note: Full dd command with parameters not yet implemented\n");
}

static void cmd_uname(int argc, char **argv) {
    // Check for -a flag
    int show_all = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
            break;
        }
    }
    
    if (show_all) {
        // uname -a: print all information
        cprintf("%s %s %s %s %s\n", 
               SYSINFO_NAME, SYSINFO_HOSTNAME, ZONIX_VERSION_STRING, 
               SYSINFO_VERSION, SYSINFO_MACHINE);
    } else {
        // Simple uname without arguments shows kernel name
        cprintf("%s\n", SYSINFO_NAME);
    }
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    print_all_procs();
}

// Global FAT file system info
// System disk (boot disk, always mounted)
static FatInfo g_system_fat{};
static const char *g_system_device{};
static int g_system_mounted{};

// /mnt mounted disk (optional)
static FatInfo g_mnt_fat{};
static const char *g_mnt_device{};
static int g_mnt_mounted{};

// Auto-mount system disk (hda) on first access
static int ensure_system_mounted(void) {
    if (g_system_mounted) {
        return 0;  // Already mounted
    }
    
    // Try to mount hda (system disk)
    block_device_t* dev = blk_get_device_by_name("hda");
    if (!dev) {
        cprintf("Error: System disk (hda) not found\n");
        return -1;
    }
    
    if (g_system_fat.mount(dev) == 0) {
        g_system_device = dev->name;
        g_system_mounted = 1;
        return 0;
    } else {
        cprintf("Error: Failed to mount system disk\n");
        return -1;
    }
}

static void cmd_mount(int argc, char **argv) {
    // Check if /mnt already has something mounted
    if (g_mnt_mounted) {
        cprintf("Device already mounted at /mnt: %s\n", g_mnt_device);
        cprintf("Use 'umount' to unmount first\n");
        return;
    }
    
    // Parse arguments
    if (argc < 2) {
        cprintf("Usage: mount <device>\n");
        cprintf("Example: mount hdb\n");
        return;
    }

    const char *dev_name = argv[1];
    
    // Don't allow mounting system disk to /mnt
    if (strcmp(dev_name, "hda") == 0) {
        cprintf("Error: hda is the system disk and already mounted at /\n");
        return;
    }
    
    block_device_t *dev = blk_get_device_by_name(dev_name);
    if (!dev) {
        cprintf("Device not found: %s\n", dev_name);
        cprintf("Use 'lsblk' to see available devices\n");
        return;
    }
    
    cprintf("Mounting %s at /mnt...\n", dev->name);
    
    if (g_mnt_fat.mount(dev) == 0) {
        g_mnt_device = dev->name;
        g_mnt_mounted = 1;
        cprintf("Successfully mounted %s at /mnt\n", dev->name);
    } else {
        cprintf("Failed to mount FAT file system\n");
        cprintf("Make sure the device contains a valid FAT12/FAT16/FAT32 file system\n");
    }
}

static void cmd_umount(int argc, char **argv) {
    (void)argc; (void)argv;
    
    if (!g_mnt_mounted) {
        cprintf("Nothing mounted at /mnt\n");
        return;
    }
    
    cprintf("Unmounting %s from /mnt...\n", g_mnt_device);
    g_mnt_fat.unmount();
    g_mnt_device = nullptr;
    g_mnt_mounted = 0;
    cprintf("Successfully unmounted /mnt\n");
}

static void cmd_info(int argc, char **argv) {
    (void)argc; (void)argv;
    
    // Auto-mount system disk if needed
    if (ensure_system_mounted() != 0) {
        return;
    }
    
    cprintf("System Disk Information:\n");
    cprintf("  Device: %s\n", g_system_device);
    cprintf("  Mount Point: /\n");
    g_system_fat.print_info();
    
    if (g_mnt_mounted) {
        cprintf("\n/mnt Information:\n");
        cprintf("  Device: %s\n", g_mnt_device);
        cprintf("  Mount Point: /mnt\n");
        cprintf("  Type: FAT%d\n", g_mnt_fat.m_fat_type);
        cprintf("  Total Size: %d MB\n", 
                (g_mnt_fat.m_total_sectors * g_mnt_fat.m_bytes_per_sector) / (1024 * 1024));
    }
}

static int ls_callback(fat_dir_entry_t *entry, void *arg) {
    (void)arg;
    
    char filename[13];
    FatInfo::get_filename(entry, filename, sizeof(filename));
    
    // Get file attributes
    char attr_str[6] = "-----";
    if (entry->attr & FAT_ATTR_DIRECTORY) attr_str[0] = 'd';
    if (entry->attr & FAT_ATTR_READ_ONLY) attr_str[1] = 'r';
    if (entry->attr & FAT_ATTR_HIDDEN)    attr_str[2] = 'h';
    if (entry->attr & FAT_ATTR_SYSTEM)    attr_str[3] = 's';
    if (entry->attr & FAT_ATTR_ARCHIVE)   attr_str[4] = 'a';
    
    // Format file size
    uint32_t size = entry->file_size;
    
    cprintf("%s %8d  %s\n", attr_str, size, filename);
    
    return 0;  // Continue
}

static void cmd_ls(int argc, char **argv) {
    // Check if path is /mnt
    int use_mnt = 0;
    if (argc >= 2 && strcmp(argv[1], "/mnt") == 0) {
        use_mnt = 1;
    }
    
    FatInfo *fat_info;
    const char *path;
    
    if (use_mnt) {
        if (!g_mnt_mounted) {
            cprintf("Nothing mounted at /mnt\n");
            cprintf("Use 'mount <device>' to mount a device\n");
            return;
        }
        fat_info = &g_mnt_fat;
        path = "/mnt";
    } else {
        // Auto-mount system disk if needed
        if (ensure_system_mounted() != 0) {
            return;
        }
        fat_info = &g_system_fat;
        path = "/";
    }
    
    cprintf("Directory listing of %s:\n", path);
    cprintf("ATTR     SIZE     NAME\n");
    cprintf("-------- -------- ------------\n");
    
    int count = fat_info->read_root_dir(ls_callback, nullptr);
    
    if (count < 0) {
        cprintf("Failed to read directory\n");
    } else {
        cprintf("\nTotal: %d file(s)\n", count);
    }
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        cprintf("Usage: cat <filename> [/mnt]\n");
        cprintf("  cat <filename>     - read file from system disk (/)\n");
        cprintf("  cat <filename> /mnt - read file from mounted disk (/mnt)\n");
        return;
    }
    
    const char *filename = argv[1];
    
    // Check if reading from /mnt
    int use_mnt = 0;
    if (argc >= 3 && strcmp(argv[2], "/mnt") == 0) {
        use_mnt = 1;
    }
    
    FatInfo *fat_info;
    
    if (use_mnt) {
        if (!g_mnt_mounted) {
            cprintf("Nothing mounted at /mnt\n");
            return;
        }
        fat_info = &g_mnt_fat;
    } else {
        // Auto-mount system disk if needed
        if (ensure_system_mounted() != 0) {
            return;
        }
        fat_info = &g_system_fat;
    }
    
    fat_dir_entry_t entry;
    
    // Find file
    if (fat_info->find_file(filename, &entry) != 0) {
        cprintf("File not found: %s\n", filename);
        return;
    }
    
    // Check if it's a directory
    if (entry.attr & FAT_ATTR_DIRECTORY) {
        cprintf("Cannot cat a directory\n");
        return;
    }
    
    // Allocate buffer for file contents (max 64KB for now)
    uint32_t max_size = 65536;
    uint32_t size = entry.file_size;
    if (size > max_size) {
        cprintf("File too large (max %d bytes)\n", max_size);
        size = max_size;
    }
    
    if (size == 0) {
        cprintf("(empty file)\n");
        return;
    }
    
    // Use static buffer to avoid allocation
    static uint8_t file_buf[4096];
    uint32_t offset = 0;
    
    cprintf("--- File: %s (%d bytes) ---\n", filename, entry.file_size);
    
    while (offset < size) {
        uint32_t chunk_size = size - offset;
        if (chunk_size > sizeof(file_buf)) {
            chunk_size = sizeof(file_buf);
        }
        
        int read = fat_info->read_file(&entry, file_buf, offset, chunk_size);
        if (read <= 0) {
            cprintf("\nError reading file at offset %d\n", offset);
            break;
        }
        
        // Print contents
        for (int i = 0; i < read; i++) {
            char c = file_buf[i];
            if (c == '\n') {
                cons_putc('\n');
            } else if (c == '\r') {
                // Skip CR
            } else if (c >= 32 && c < 127) {
                cons_putc(c);
            } else {
                cons_putc('.');  // Non-printable
            }
        }
        
        offset += read;
    }
    
    cprintf("\n--- End of file ---\n");
}

// Command table
shell_cmd_t commands[] = {
    {"help",       "Show this help message", cmd_help},
    {"pgdir",      "Print page directory", cmd_pgdir},
    {"clear",      "Clear the screen", cmd_clear},
    {"swaptest",   "Run swap system tests", cmd_swap_test},
    {"lsblk",      "List block devices", cmd_lsblk},
    {"hdparm",     "Show disk information", cmd_hdparm},
    {"disktest",   "Test disk read/write", cmd_disktest},
    {"intrtest",   "Test IDE interrupts", cmd_intrtest},
    {"dd",         "Disk dump/copy (info only)", cmd_dd},
    {"uname",      "Print system information (-a for all)", cmd_uname},
    {"ps",         "List all processes", cmd_ps},
    {"mount",      "Mount device to /mnt (usage: mount <device>)", cmd_mount},
    {"umount",     "Unmount /mnt", cmd_umount},
    {"info",       "Show file system information", cmd_info},
    {"ls",         "List files (usage: ls [/mnt])", cmd_ls},
    {"cat",        "Display file contents (usage: cat <file> [/mnt])", cmd_cat},
};

int command_count = sizeof(commands) / sizeof(shell_cmd_t);

// Parse command line into arguments
// Returns number of arguments parsed
static int parse_args(const char *cmd, char **argv) {
    static char arg_buf[CMD_BUF_SIZE];
    int argc = 0;
    
    // Copy to buffer for manipulation
    int i = 0;
    while (cmd[i] && i < CMD_BUF_SIZE - 1) {
        arg_buf[i] = cmd[i];
        i++;
    }
    arg_buf[i] = '\0';
    
    // Parse arguments
    char *p = arg_buf;
    while (*p && argc < MAX_ARGS) {
        // Skip leading spaces
        while (*p == ' ') p++;
        
        if (*p == '\0') break;
        
        // Start of argument
        argv[argc++] = p;
        
        // Find end of argument
        while (*p && *p != ' ') p++;
        
        // Null-terminate if not end of string
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    
    return argc;
}

static void execute_command(const char *cmd) {
    char *argv[MAX_ARGS];
    
    // Skip leading spaces
    while (*cmd == ' ') cmd++;
    
    // Empty command
    if (*cmd == '\0') {
        return;
    }
    
    // Parse arguments
    int argc = parse_args(cmd, argv);
    if (argc == 0) {
        return;
    }
    
    // Find and execute command
    for (int i = 0; i < command_count; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }
    
    // Command not found
    cprintf("Unknown command: %s\n", argv[0]);
    cprintf("Type 'help' for available commands.\n");
}

static int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        return 0;
    }
    return (*(unsigned char *)s1 - *(unsigned char *)s2);
}

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (*(unsigned char *)s1 - *(unsigned char *)s2);
}

void shell_prompt(void) {
    cprintf("zonix> ");
}

void shell_init(void) {
    cmd_pos = 0;
    cmd_buffer[0] = '\0';
    cprintf("\n");
    cprintf("=============================================\n");
    cprintf("  Welcome to Zonix OS Interactive Console\n");
    cprintf("  Type 'help' to see available commands\n");
    cprintf("=============================================\n");
    // Don't print prompt yet - wait until system is fully ready
}

void shell_handle_char(char c) {
    if (c <= 0) {
        return;  // Invalid character
    }
    
    switch (c) {
        case '\n':
        case '\r':
            // Execute command
            cons_putc('\n');
            cmd_buffer[cmd_pos] = '\0';
            execute_command(cmd_buffer);
            cmd_pos = 0;
            shell_prompt();
            break;
            
        case '\b':
            // Backspace
            if (cmd_pos > 0) {
                cmd_pos--;
                cons_putc('\b');
            }
            break;
            
        case ASCII_DEL:
            // Ignore DEL key
            break;
            
        default:
            // Regular printable character
            if (cmd_pos < CMD_BUF_SIZE - 1 && 
                c >= ASCII_PRINTABLE_MIN && c < ASCII_PRINTABLE_MAX) {
                cmd_buffer[cmd_pos++] = c;
                cons_putc(c);
            }
            break;
    }
}
