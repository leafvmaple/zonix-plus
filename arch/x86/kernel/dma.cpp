#include "mm/dma.h"

namespace dma {

// PCI devices snoop the CPU caches on this architecture.
void writeback_invalidate(uintptr_t, size_t) {}

}  // namespace dma
