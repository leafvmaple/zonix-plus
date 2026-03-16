#include "block/blk.h"
#include "drivers/ide.h"
#include "drivers/ahci.h"
#include "drivers/i8259.h"

#include <asm/drivers/i8259.h>

#include "lib/stdio.h"

namespace blk {

int probe_backends() {
    i8259::enable(IRQ_IDE1);
    i8259::enable(IRQ_IDE2);

    cprintf("blk: probing IDE devices...\n");
    IdeManager::init();
    int ide_count = IdeManager::get_device_count();
    for (int i = 0; i < ide_count; i++) {
        IdeDevice* dev = IdeManager::get_device(i);
        if (dev != nullptr) {
            dev->size = dev->info.size;
            BlockManager::register_device(dev);
            cprintf("blk: registered IDE device '%s' (%d sectors)\n", dev->name, dev->info.size);
        }
    }
    if (ide_count == 0) {
        cprintf("blk: no IDE devices found\n");
    }

    cprintf("blk: probing AHCI devices...\n");
    AhciManager::init();
    int ahci_count = AhciManager::get_device_count();
    for (int i = 0; i < ahci_count; i++) {
        AhciDevice* dev = AhciManager::get_device(i);
        if (dev != nullptr) {
            dev->size = dev->info.size;
            BlockManager::register_device(dev);
            cprintf("blk: registered AHCI device '%s' (%d sectors)\n", dev->name, dev->info.size);
        }
    }
    if (ahci_count == 0) {
        cprintf("blk: no AHCI devices found\n");
    }

    return 0;
}

}  // namespace blk
