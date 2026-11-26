#pragma once

#include <base/types.h>
#include "lib/result.h"

// The kernel's source of random bytes for getrandom. A hardware generator on
// the machine itself comes first; otherwise the VirtIO entropy device answers.
namespace entropy {

constexpr size_t MAX_READ = 256;

// Starts the machine's own generator. Returns 0 on machines without one too.
int init();
// Fills up to `size` bytes (at most MAX_READ) and returns how many it filled.
Result<size_t> read(void* output, size_t size);

// Implemented by each architecture. start_platform prepares the machine's
// generator, returning NotSupported when there is none; read_platform fills
// up to `size` bytes once it has started.
Error start_platform();
Result<size_t> read_platform(void* output, size_t size);

}  // namespace entropy
