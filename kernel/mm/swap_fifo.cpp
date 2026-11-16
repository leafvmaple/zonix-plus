#include "lib/stdio.h"
#include "vmm.h"
#include "swap.h"
#include "drivers/intr.h"

// Pages are arranged in a queue - first in, first out

Error SwapManager::init() {
    name = "FIFO Page Replacement Algorithm";
    return Error::None;
}

Error SwapManager::init_mm(MemoryDesc* mm) {
    // Per-mm swap queue: do not share FIFO state across address spaces.
    ENSURE(mm);
    return Error::None;
}

Error SwapManager::map_swappable(MemoryDesc* mm, uintptr_t va, Page* page, int from_swap_in) {
    ENSURE(mm && page && !page->list_node.is_linked());
    intr::Guard guard;
    mm->swap_list.push_back(*page);

    return Error::None;
}

Error SwapManager::swap_out_victim(MemoryDesc* mm, Page** page_ptr, int in_timer_tick) {
    ENSURE(mm && page_ptr);
    intr::Guard guard;
    if (mm->swap_list.empty()) {
        *page_ptr = nullptr;
        return Error::NotFound;
    }

    Page& victim = mm->swap_list.front();
    mm->swap_list.erase(mm->swap_list.begin());
    *page_ptr = &victim;
    return Error::None;
}
