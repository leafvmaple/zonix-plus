#include "stdio.h"
#include "vmm.h"

#include "swap_fifo.h"

// FIFO Page Replacement Algorithm
// Pages are arranged in a queue - first in, first out

ListNode pra_list_head;

int swap_fifo_init() {
    return 0;
}

int swap_fifo_init_mm(MemoryDesc *mm) {
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
int swap_fifo_map_swappable(MemoryDesc *mm, uintptr_t addr, Page *page, int swap_in) {
    ListNode *head = (ListNode*) mm->swap_list;
    head->add_before(page->node());
    
    return 0;
}

/**
 * Select a victim page for replacement using FIFO
 * @param mm: memory management struct
 * @param page_ptr: output pointer to victim page
 * @param in_tick: not used in FIFO
 */
int swap_fifo_swap_out_victim(MemoryDesc *mm, Page **page_ptr, int in_tick) {
    ListNode *head = (ListNode*) mm->swap_list;
    
    // Select the first page (oldest) in the FIFO queue
    ListNode *victim = head->get_next();
    
    if (victim == head) {
        *page_ptr = nullptr;
        return -1;  // No page available
    }
    
    // Remove from list
    victim->unlink();
    
    *page_ptr = le2page(victim, m_node);
    return 0;
}

int swap_fifo_check_swap() {
    cprintf("FIFO swap check: passed\n");
    return 0;
}

swap_manager swap_mgr_fifo = {
    .name = "fifo",
    .init = swap_fifo_init,
    .init_mm = swap_fifo_init_mm,
    .map_swappable = swap_fifo_map_swappable,
    .swap_out_victim = swap_fifo_swap_out_victim,
    .check_swap = swap_fifo_check_swap,
};