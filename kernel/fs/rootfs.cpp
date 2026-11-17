#include "rootfs.h"
#include <kernel/log.h>

#include "fs/vfs.h"
#include "block/blk.h"
#include "lib/stdio.h"

namespace rootfs {

int init() {
    int count = BlockManager::device_count();

    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::find_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }

        if (vfs::mount("/", dev, "fat") == Error::None) {
            klog::info("rootfs: %s mounted at / (fat)\n", dev->name);
            return 0;
        }
    }

    klog::warn("rootfs: no mountable system disk found\n");
    return -1;
}

int init_user() {
    const char* system = vfs::mounted_device("/");
    for (int i = 0; i < BlockManager::device_count(); ++i) {
        auto* device = BlockManager::find_device(i);
        if (!device || device->type != blk::DeviceType::Disk || (system && strcmp(system, device->name) == 0))
            continue;
        if (vfs::mount("/user", device, "fat") == Error::None) {
            klog::info("rootfs: %s mounted at /user (fat)\n", device->name);
            return 0;
        }
    }
    klog::error("rootfs: missing user disk; prepare it with make user-image\n");
    return -1;
}

}  // namespace rootfs
