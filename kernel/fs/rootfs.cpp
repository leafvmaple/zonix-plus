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

        if (vfs::mount("/", dev, "fat") != Error::None)
            continue;
#ifndef TEST_MODE
        vfs::Stat init{};
        if (vfs::stat("/sbin/init", &init) != Error::None || init.type != vfs::NodeType::File) {
            if (vfs::umount("/") != Error::None) {
                klog::error("rootfs: cannot release candidate %s\n", dev->name);
                return -1;
            }
            continue;
        }
#endif
        klog::info("rootfs: %s mounted at / (fat)\n", dev->name);
        return 0;
    }

    klog::warn("rootfs: no suitable root filesystem found\n");
    return -1;
}

int init_apps() {
    const char* system = vfs::mounted_device("/");
    for (int i = 0; i < BlockManager::device_count(); ++i) {
        auto* device = BlockManager::find_device(i);
        if (!device || device->type != blk::DeviceType::Disk || (system && strcmp(system, device->name) == 0))
            continue;
        if (vfs::mount("/opt", device, "fat") != Error::None)
            continue;
        vfs::Stat bin{};
        if (vfs::stat("/opt/bin", &bin) == Error::None && bin.type == vfs::NodeType::Directory) {
            klog::info("apps: %s mounted at /opt (fat)\n", device->name);
            return 0;
        }
        if (vfs::umount("/opt") != Error::None) {
            klog::error("apps: cannot release candidate %s\n", device->name);
            return -1;
        }
    }
    klog::info("apps: no application disk; continuing with base system\n");
    return 0;
}

}  // namespace rootfs
