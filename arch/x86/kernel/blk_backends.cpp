#include "block/blk.h"
#include "drivers/ide.h"
#include "drivers/i8259.h"

#include <asm/drivers/i8259.h>

#include "lib/stdio.h"

namespace blk {

int probe_backends() {
    i8259::enable(IRQ_IDE1);
    i8259::enable(IRQ_IDE2);

    cprintf("blk: probing IDE devices...\n");
    Error error = IdeManager::init();
    int ide_count = IdeManager::device_count();
    if (ide_count == 0) {
        cprintf("blk: no IDE devices found\n");
    }

    return static_cast<int>(error);
}

}  // namespace blk
