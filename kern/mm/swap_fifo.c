#include "stdio.h"
#include "vmm.h"

#include "swap_fifo.h"

// FIFO Page Replacement Algorithm
// Pages are arranged in a queue - first in, first out

list_entry_t pra_list_head;

int swap_fifo_init() {
    return 0;
}

int swap_fifo_init_mm(mm_struct *mm) {
    list_init(&pra_list_head);
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
int swap_fifo_map_swappable(mm_struct *mm, uintptr_t addr, PageDesc *page, int swap_in) {
    list_entry_t *head = (list_entry_t*) mm->swap_list;
    list_entry_t *entry = &(page->page_link);
    
    // Add page to the end of FIFO queue
    list_add_before(head, entry);
    
    return 0;
}

/**
 * Select a victim page for replacement using FIFO
 * @param mm: memory management struct
 * @param page_ptr: output pointer to victim page
 * @param in_tick: not used in FIFO
 */
int swap_fifo_swap_out_victim(mm_struct *mm, PageDesc **page_ptr, int in_tick) {
    list_entry_t *head = (list_entry_t*) mm->swap_list;
    
    // Select the first page (oldest) in the FIFO queue
    list_entry_t *victim = list_next(head);
    
    if (victim == head) {
        *page_ptr = NULL;
        return -1;  // No page available
    }
    
    // Remove from list
    list_del(victim);
    
    *page_ptr = le2page(victim, page_link);
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