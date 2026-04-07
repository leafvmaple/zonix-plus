#include "blk.h"

#include "lib/stdio.h"
#include "lib/string.h"

void BlockManager::init() {
    cprintf("blk_init: initializing block device layer...\n");
}

void BlockManager::register_device(BlockDevice* device) {
    if (!s_devices.push_back(device)) {
        cprintf("BlockManager::register_device: too many devices\n");
    }
}

BlockDevice* BlockManager::get_device(const char* device_name) {
    if (!device_name) {
        return nullptr;
    }

    for (BlockDevice* dev : s_devices) {
        if (dev && strcmp(dev->name, device_name) == 0) {
            return dev;
        }
    }
    return nullptr;
}

BlockDevice* BlockManager::get_device(int index) {
    if (index < 0 || static_cast<size_t>(index) >= s_devices.size()) {
        return nullptr;
    }
    return s_devices[index];
}

BlockDevice* BlockManager::get_device(blk::DeviceType type) {
    for (BlockDevice* dev : s_devices) {
        if (dev && dev->type == type) {
            return dev;
        }
    }
    return nullptr;
}

int BlockManager::get_device_count() {
    return static_cast<int>(s_devices.size());
}

void BlockDevice::print_info() {
    cprintf("Device: %s\n", name);
    cprintf("  Size: %d sectors (%d MB)\n", size, size / 2048);
    cprintf("\n");
}

void BlockManager::print() {
    cprintf("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");

    for (size_t i = 0; i < s_devices.size(); i++) {
        BlockDevice* dev = s_devices[i];
        if (dev) {
            const char* type_string = "disk";
            const char* mount_string = "";

            if (dev->type == blk::DeviceType::Swap) {
                mount_string = "[SWAP]";
            }

            uint32_t size_bytes = dev->size * BlockDevice::SIZE;
            uint32_t size_mb = size_bytes / (1024 * 1024);
            uint32_t remainder = size_bytes % (1024 * 1024);
            uint32_t decimal = (remainder * 10) / (1024 * 1024);

            cprintf("%-6s %3d:%-3d %-2d %2d.%dM %-2d %-4s %s\n", dev->name, 8, static_cast<int>(i) * 16, 0, size_mb,
                    decimal, 0, type_string, mount_string);
        }
    }
}

namespace blk {

int init() {
    BlockManager::init();

    int rc = probe_backends();
    if (rc != 0) {
        cprintf("blk: probe_backends failed (rc=%d)\n", rc);
        return rc;
    }

    int count = BlockManager::get_device_count();
    cprintf("blk: %d device(s) registered (before PCI probe)\n", count);
    return 0;
}

Error register_device(BlockDevice* device) {
    ENSURE(device, Error::Invalid);
    BlockManager::register_device(device);
    return Error::None;
}

}  // namespace blk
