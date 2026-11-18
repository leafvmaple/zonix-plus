#pragma once

#include <base/types.h>
#include "lib/result.h"

namespace virtio_input {

int init();
void intr(unsigned int irq);

}  // namespace virtio_input
