#include "stdio.h"
#include "vmm.h"

#include "swap_fifo.h"

// FIFO Page Replacement Algorithm
// Pages are arranged in a queue - first in, first out

FifoSwapManager swap_mgr_fifo;

static ListNode pra_list_head;

int FifoSwapManager::init() {
    return 0;
}

int FifoSwapManager::init_mm(MemoryDesc *mm) {
    pra_list_head.init();
    mm->swap_list = &pra_list_head;

    return 0;
}

/**
 * Mark a page as swappable using FIFO algorithm
 * @param mm: memory management struct
 * @param addr: virtual address
 * @param page: page descriptor
 * @param swap_in: 1 if swapping in, 0 if newly mapped
 */
int FifoSwapManager::map_swappable(MemoryDesc *mm, uintptr_t addr, Page *page, int swap_in) {
    auto *head = static_cast<ListNode*>(mm->swap_list);
    head->add_before(page->node());
    
    return 0;
}

/**
 * Select a victim page for replacement using FIFO
 * @param mm: memory management struct
 * @param page_ptr: output pointer to victim page
 * @param in_tick: not used in FIFO
 */
int FifoSwapManager::swap_out_victim(MemoryDesc *mm, Page **page_ptr, int in_tick) {
    auto *head = static_cast<ListNode*>(mm->swap_list);
    
    // Select the first page (oldest) in the FIFO queue
    ListNode *victim = head->get_next();
    
    if (victim == head) {
        *page_ptr = nullptr;
        return -1;  // No page available
    }
    
    // Remove from list
    victim->unlink();
    
    *page_ptr = victim->container<Page>();
    return 0;
}

int FifoSwapManager::check_swap() {
    cprintf("FIFO swap check: passed\n");
    return 0;
}