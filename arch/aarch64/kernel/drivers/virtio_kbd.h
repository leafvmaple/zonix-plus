#pragma once

#include <base/types.h>

namespace virtio_kbd {

int init();
void intr();           // called from IRQ handler
uint32_t gic_intid();  // return the GIC IntID for this device

}  // namespace virtio_kbd
