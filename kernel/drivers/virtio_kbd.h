#pragma once

#include <base/types.h>

namespace virtio_kbd {

int init();
void intr();  // called from IRQ handler
int irq();    // return the platform IRQ number for this device

}  // namespace virtio_kbd
