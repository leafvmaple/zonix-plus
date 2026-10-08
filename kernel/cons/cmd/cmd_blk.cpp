#include "cmd.h"

#include "block/blk.h"
#include "fs/vfs.h"
#include "lib/memory.h"
#include "lib/result.h"
#include "lib/stdio.h"
#include "lib/string.h"

namespace cmd {

namespace {

const char* mounted_device{};
int mount_is_active{};

}  // namespace

const char* mnt_device() {
    return mounted_device;
}

int mnt_mounted() {
    return mount_is_active;
}

void set_mnt_device(const char* name) {
    mounted_device = name;
}

void set_mnt_mounted(int val) {
    mount_is_active = val;
}

int build_path(const char* filename, int use_mnt, char* out, size_t out_size) {
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

static void cmd_lsblk(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    BlockManager::print();
}

static void cmd_hdparm(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    int count = BlockManager::device_count();
    if (count == 0) {
        cprintf("No disk devices found\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::find_device(i);
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

static void cmd_mount(int argc, char** argv) {
    if (mount_is_active || vfs::is_mounted("/mnt")) {
        const char* mounted = mounted_device ? mounted_device : vfs::mounted_device("/mnt");
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

    BlockDevice* dev = BlockManager::find_device(dev_name);
    if (!dev) {
        cprintf("Device not found: %s\n", dev_name);
        cprintf("Use 'lsblk' to see available devices\n");
        return;
    }

    cprintf("Mounting %s at /mnt...\n", dev->name);

    if (vfs::mount("/mnt", dev, "fat") == Error::None) {
        mounted_device = dev->name;
        mount_is_active = 1;
        cprintf("Successfully mounted %s at /mnt\n", dev->name);
    } else {
        cprintf("Failed to mount file system\n");
        cprintf("Make sure the device contains a valid FAT12/FAT16/FAT32 file system\n");
    }
}

static void cmd_umount(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    if (!mount_is_active || !vfs::is_mounted("/mnt")) {
        cprintf("Nothing mounted at /mnt\n");
        mount_is_active = 0;
        mounted_device = nullptr;
        return;
    }

    if (vfs::umount("/mnt") != Error::None) {
        cprintf("Failed to unmount /mnt\n");
        return;
    }

    cprintf("Unmounting %s from /mnt...\n", mounted_device);
    mounted_device = nullptr;
    mount_is_active = 0;
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

    if (mount_is_active) {
        cprintf("\n/mnt Information:\n");
        cprintf("  Device: %s\n", mounted_device);
        cprintf("  Mount Point: /mnt\n");
        vfs::print_mount_info("/mnt");
    }
}

void register_blk_commands() {
    shell::register_command("lsblk", "List block devices", cmd_lsblk);
    shell::register_command("hdparm", "Show disk information", cmd_hdparm);
    shell::register_command("dd", "Disk dump/copy (info only)", cmd_dd);
    shell::register_command("mount", "Mount device to /mnt (usage: mount <device>)", cmd_mount);
    shell::register_command("umount", "Unmount /mnt", cmd_umount);
    shell::register_command("info", "Show file system information", cmd_info);
}

}  // namespace cmd
