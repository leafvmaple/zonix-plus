#include "block/blk.h"
#include "drivers/ide.h"
#include "drivers/ahci.h"
#include "drivers/pic.h"

#include <asm/drivers/i8259.h>

#include "lib/stdio.h"

namespace blk {

void probe_backends() {
    pic::enable(IRQ_IDE1);
    pic::enable(IRQ_IDE2);

    IdeManager::init();
    int ide_count = IdeManager::get_device_count();
    for (int i = 0; i < ide_count; i++) {
        IdeDevice* dev = IdeManager::get_device(i);
        if (dev != nullptr) {
            dev->size = dev->info.size;
            BlockManager::register_device(dev);
        }
    }
    if (ide_count == 0) {
        cprintf("blk_init: no IDE disk devices found\n");
    }

    AhciManager::init();
    int ahci_count = AhciManager::get_device_count();
    for (int i = 0; i < ahci_count; i++) {
        AhciDevice* dev = AhciManager::get_device(i);
        if (dev != nullptr) {
            dev->size = dev->info.size;
            BlockManager::register_device(dev);
        }
    }
    if (ahci_count == 0) {
        cprintf("blk_init: no AHCI disk devices found\n");
    }
}

}  // namespace blk
