#pragma once

#include <base/types.h>

namespace virtio_gpu {

// Initialise the virtio-gpu device and set up a framebuffer scanout.
// Call after PCI and VMM are available.
// On success the framebuffer can be used via fbcons.
int init();

// Transfer framebuffer contents to host and flush display.
void flush();

}  // namespace virtio_gpu
