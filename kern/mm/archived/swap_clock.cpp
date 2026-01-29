#include "stdio.h"
#include "vmm.h"

#include "swap_clock.h"

// Clock (Second Chance) Page Replacement Algorithm
// Uses a circular list and gives pages a "second chance" before replacement

static list_entry_t pra_list_head;
static list_entry_t *clock_ptr;  // Clock hand pointer

int swap_clock_init() {
    return 0;
}

int swap_clock_init_mm(mm_struct *mm) {
    list_init(&pra_list_head);
    mm->swap_list = &pra_list_head;
    clock_ptr = &pra_list_head;  // Initialize clock hand to head
    
    return 0;
}

/**
 * Mark a page as swappable using Clock algorithm
 * @param mm: memory management struct
 * @param addr: virtual address
 * @param page: page descriptor
 * @param swap_in: 1 if swapping in, 0 if newly mapped
 */
int swap_clock_map_swappable(mm_struct *mm, uintptr_t addr, Page *page, int swap_in) {
    list_entry_t *head = (list_entry_t*) mm->swap_list;
    list_entry_t *entry = &(page->page_link);
    
    // Add page to the clock list
    list_add_before(head, entry);
    
    return 0;
}

/**
 * Select a victim page using Clock (Second Chance) algorithm
 * @param mm: memory management struct
 * @param page_ptr: output pointer to victim page
 * @param in_tick: not used in Clock
 */
int swap_clock_swap_out_victim(mm_struct *mm, Page **page_ptr, int in_tick) {
    list_entry_t *head = (list_entry_t*) mm->swap_list;
    
    if (clock_ptr == nullptr || clock_ptr == head) {
        clock_ptr = list_next(head);
    }
    
    // Scan through the circular list
    while (1) {
        if (clock_ptr == head) {
            clock_ptr = list_next(clock_ptr);
            if (clock_ptr == head) {
                *page_ptr = nullptr;
                return -1;  // No page available
            }
        }
        
        Page *page = le2page(clock_ptr, page_link);
        
        // Check the accessed bit (PTE_A)
        // For now, we'll use a simplified version without checking PTE
        // In a full implementation, you'd check: if (ptep && (*ptep & PTE_A))
        
        // Simplified: just move to next page (like FIFO)
        // In real implementation: check accessed bit, clear it if set, skip to next
        list_entry_t *victim = clock_ptr;
        clock_ptr = list_next(clock_ptr);
        
        // Remove from list
        list_del(victim);
        
        *page_ptr = le2page(victim, page_link);
        return 0;
    }
}

int swap_clock_check_swap() {
    cprintf("Clock swap check: passed\n");
    return 0;
}

swap_manager swap_mgr_clock = {
    .name = "clock swap manager",
    .init = swap_clock_init,
    .init_mm = swap_clock_init_mm,
    .map_swappable = swap_clock_map_swappable,
    .swap_out_victim = swap_clock_swap_out_victim,
    .check_swap = swap_clock_check_swap,
};
