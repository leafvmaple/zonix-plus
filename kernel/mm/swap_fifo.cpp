#include "lib/stdio.h"
#include "vmm.h"
#include "swap.h"

// Pages are arranged in a queue - first in, first out

Error SwapManager::init() {
    name = "FIFO Page Replacement Algorithm";
    return Error::None;
}

Error SwapManager::init_mm(MemoryDesc* mm) {
    // Per-mm swap queue: do not share FIFO state across address spaces.
    return Error::None;
}

Error SwapManager::map_swappable(MemoryDesc* mm, uintptr_t addr, Page* page, int swap_in) {
    mm->swap_list.add_before(page->node());

    return Error::None;
}

Error SwapManager::swap_out_victim(MemoryDesc* mm, Page** page_ptr, int in_tick) {
    if (mm->swap_list.empty()) {
        *page_ptr = nullptr;
        return Error::NotFound;
    }

    ListNode* victim = mm->swap_list.get_next();

    // Remove from list
    victim->unlink();

    *page_ptr = victim->container<Page>();
    return Error::None;
}
