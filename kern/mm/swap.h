#pragma once

#include "pmm.h"
#include "vmm.h"

// Swap manager interface
typedef struct {
    const char *name;
    int (*init)(void);                     // Initialize swap manager
    int (*init_mm)(mm_struct *mm);         // Initialize mm struct for swap
    int (*map_swappable)(mm_struct *mm, uintptr_t addr, Page *page, int swap_in);
    int (*swap_out_victim)(mm_struct *mm, Page **page_ptr, int in_tick);
    int (*check_swap)(void);               // Check if swap works correctly
} swap_manager;

// Page-to-address mapping entry (for reverse lookup)
typedef struct {
    Page *page;
    uintptr_t addr;
    list_entry_t link;
} page_addr_map_t;

// Global functions
int swap_init();
int swap_init_mm(mm_struct *mm);
int swap_in(mm_struct *mm, uintptr_t addr, Page **page_ptr);
int swap_out(mm_struct *mm, int n, int in_tick);

// Swap disk operations (to be implemented with disk driver)
int swapfs_init(void);
int swapfs_read(uintptr_t entry, Page *page);
int swapfs_write(uintptr_t entry, Page *page);

// Helper function to find virtual address for a page
uintptr_t find_vaddr_for_page(mm_struct *mm, Page *page);

#define MAX_SWAP_OFFSET_LIMIT (1 << 24)  // 16 GB swap space limit