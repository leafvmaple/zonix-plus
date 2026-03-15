#include "blk.h"

#include "lib/stdio.h"
#include "lib/string.h"

// Static member definitions
BlockDevice* BlockManager::s_devices[BlockManager::MAX_DEV] = {};
int BlockManager::s_device_count = 0;

void BlockManager::init() {
    cprintf("blk_init: initializing block device layer...\n");

    // Let each architecture/platform register its own block backends.
    blk::probe_backends();
}

void BlockManager::register_device(BlockDevice* device) {
    if (s_device_count >= MAX_DEV) {
        cprintf("BlockManager::register_device: too many devices\n");
        return;
    }

    s_devices[s_device_count++] = device;
}

BlockDevice* BlockManager::get_device(const char* device_name) {
    if (!device_name) {
        return nullptr;
    }

    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] && strcmp(s_devices[i]->name, device_name) == 0) {
            return s_devices[i];
        }
    }
    return nullptr;
}

BlockDevice* BlockManager::get_device(int index) {
    if (index < 0 || index >= s_device_count) {
        return nullptr;
    }
    return s_devices[index];
}

BlockDevice* BlockManager::get_device(blk::DeviceType type) {
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] && s_devices[i]->type == type) {
            return s_devices[i];
        }
    }
    return nullptr;
}

int BlockManager::get_device_count() {
    return s_device_count;
}

void BlockDevice::print_info() {
    cprintf("Device: %s\n", name);
    cprintf("  Size: %d sectors (%d MB)\n", size, size / 2048);
    cprintf("\n");
}

void BlockDevice::test() {
    cprintf("%s: device test not implemented\n", name);
}

void BlockDevice::test_interrupt() {
    cprintf("%s: interrupt test not implemented\n", name);
}

void BlockManager::print() {
    cprintf("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");

    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i]) {
            const char* type_string = "disk";
            const char* mount_string = "";

            if (s_devices[i]->type == blk::DeviceType::Swap) {
                mount_string = "[SWAP]";
            }

            uint32_t size_bytes = s_devices[i]->size * BlockDevice::SIZE;
            uint32_t size_mb = size_bytes / (1024 * 1024);
            uint32_t remainder = size_bytes % (1024 * 1024);
            uint32_t decimal = (remainder * 10) / (1024 * 1024);

            cprintf("%-6s %3d:%-3d %-2d %2d.%dM %-2d %-4s %s\n", s_devices[i]->name, 8, i * 16, 0, size_mb, decimal, 0,
                    type_string, mount_string);
        }
    }
}

void BlockManager::test_devices() {
    if (s_device_count == 0) {
        cprintf("No block devices found\n");
        return;
    }

    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] != nullptr) {
            s_devices[i]->test();
        }
    }
}

void BlockManager::test_interrupts() {
    if (s_device_count == 0) {
        cprintf("No block devices found\n");
        return;
    }

    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] != nullptr) {
            s_devices[i]->test_interrupt();
        }
    }
}

namespace blk {

void init() {
    BlockManager::init();
}

void test_devices() {
    BlockManager::test_devices();
}

void test_interrupts() {
    BlockManager::test_interrupts();
}

}  // namespace blk
