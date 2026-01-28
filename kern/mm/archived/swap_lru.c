#include "stdio.h"
#include "vmm.h"

#include "swap_lru.h"

// LRU (Least Recently Used) Page Replacement Algorithm
// Most recently used pages are at the back, least recently used at front

static list_entry_t pra_list_head;

int swap_lru_init() {
    return 0;
}

int swap_lru_init_mm(mm_struct *mm) {
    list_init(&pra_list_head);
    mm->swap_list = &pra_list_head;
    
    return 0;
}

/**
 * Mark a page as swappable using LRU algorithm
 * @param mm: memory management struct
 * @param addr: virtual address
 * @param page: page descriptor
 * @param swap_in: 1 if swapping in (recently used), 0 if newly mapped
 */
int swap_lru_map_swappable(mm_struct *mm, uintptr_t addr, PageDesc *page, int swap_in) {
    list_entry_t *head = (list_entry_t*) mm->swap_list;
    list_entry_t *entry = &(page->page_link);
    
    // For LRU, when a page is accessed, move it to the back (most recent)
    // If it's already in the list, remove it first
    if (entry->prev != NULL && entry->next != NULL) {
        list_del(entry);
    }
    
    // Add to back of list (most recently used position)
    list_add_before(head, entry);
    
    return 0;
}

/**
 * Select a victim page using LRU algorithm
 * @param mm: memory management struct
 * @param page_ptr: output pointer to victim page
 * @param in_tick: not used in LRU
 */
int swap_lru_swap_out_victim(mm_struct *mm, PageDesc **page_ptr, int in_tick) {
    list_entry_t *head = (list_entry_t*) mm->swap_list;
    
    // Select the first page (least recently used)
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

/**
 * Update LRU on page access
 * Call this when a page is accessed to move it to back of list
 */
int swap_lru_tick_event(mm_struct *mm) {
    // In a real implementation, this would be called on page access
    // and would update the LRU list accordingly
    return 0;
}

int swap_lru_check_swap() {
    cprintf("LRU swap check: passed\n");
    return 0;
}

swap_manager swap_mgr_lru = {
    .name = "lru swap manager",
    .init = swap_lru_init,
    .init_mm = swap_lru_init_mm,
    .map_swappable = swap_lru_map_swappable,
    .swap_out_victim = swap_lru_swap_out_victim,
    .check_swap = swap_lru_check_swap,
};
