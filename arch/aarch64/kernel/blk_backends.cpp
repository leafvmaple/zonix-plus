#include "block/blk.h"
#include "drivers/pci.h"
#include "drivers/sdhci.h"
#include "drivers/virtio_kbd.h"
#include "lib/stdio.h"

#include <kernel/config.h>

namespace blk {

int probe_backends() {
#ifdef CONFIG_SDHCI
    cprintf("blk: initializing PCI...\n");
    if (pci::init() != 0) {
        cprintf("blk: PCI init failed, skipping SDHCI\n");
    } else {
        cprintf("blk: probing SDHCI (SD card) devices...\n");
        if (sdhci::init() == 0) {
            SdDevice* dev = sdhci::get_device();
            if (dev != nullptr) {
                BlockManager::register_device(dev);
                cprintf("blk: registered SD card '%s' (%d sectors)\n", dev->name, dev->size);
            }
        } else {
            cprintf("blk: no SD card found\n");
        }
        // Initialize virtio keyboard (PCI device, needs PCI subsystem)
        virtio_kbd::init();
    }
#endif

    return 0;
}

}  // namespace blk
