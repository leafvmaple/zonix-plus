#pragma once

#include "lib/result.h"

namespace virtio_rng {
constexpr size_t MAX_READ = 256;

int init();
void tick();
Result<size_t> read(void* output, size_t size);
}  // namespace virtio_rng
