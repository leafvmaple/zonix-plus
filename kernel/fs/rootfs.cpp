#include "rootfs.h"

#include "fs/vfs.h"
#include "block/blk.h"
#include "lib/stdio.h"

namespace rootfs {

int init() {
    int count = BlockManager::get_device_count();

    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }

        if (vfs::mount("/", dev, "fat") == 0) {
            cprintf("rootfs: mounted %s at /\n", dev->name);
            return 0;
        }
    }

    cprintf("rootfs: no mountable system disk found\n");
    return -1;
}

}  // namespace rootfs
