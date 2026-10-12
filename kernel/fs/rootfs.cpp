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

}  // namespace rootfs
