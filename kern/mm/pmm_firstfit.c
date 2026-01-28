#include "pmm_firstfit.h"
#include "../debug/assert.h"

// First-Fit Physical Memory Manager
// 
// Algorithm: First-Fit
// - Searches the free list linearly from the beginning
// - Allocates the first block that is large enough
// - Simple and fast for small allocations
// - May cause fragmentation at the beginning of memory
//
// Time Complexity:
// - Allocation: O(n) where n is the number of free blocks
// - Deallocation: O(n) for merging adjacent blocks
//
// Space Complexity: O(1) auxiliary space

// Constants for page management
#define PMM_SUCCESS       0
#define PMM_INVALID_PTR   NULL
#define PAGE_INIT_VALUE   0
#define TEST_ALLOC_PAGES  5

// Free area for managing available pages
free_area_t _free;

/**
 * @brief Initialize the First-Fit physical memory manager
 */
static void init() {
    list_init(&_free.free_list);
    _free.nr_free = PAGE_INIT_VALUE;
}

/**
 * @brief Initialize memory map for a contiguous block of pages
 * @param base Pointer to the first page descriptor
 * @param n Number of pages to initialize
 */
static void init_memmap(PageDesc *base, size_t n) {
    // Clear all page descriptors in the range
    for (PageDesc* p = base; p != base + n; p++) {
        p->ref = PAGE_INIT_VALUE;
        p->flags = PAGE_INIT_VALUE;
        p->property = PAGE_INIT_VALUE;
    }
    
    // Mark the first page as the start of a free block
    base->property = n;
    SET_PAGE_RESERVED(base);
    
    // Add to free list
    _free.nr_free += n;
    list_add_before(&_free.free_list, &(base->page_link));
}

/**
 * @brief Allocate n contiguous pages using first-fit algorithm
 * @param n Number of pages to allocate
 * @return Pointer to the first page descriptor, NULL if allocation failed
 */
static PageDesc* alloc(size_t n) {
    // Check if we have enough free pages
    if (n > _free.nr_free) {
        return PMM_INVALID_PTR;
    }
    
    // Search for a suitable free block (first-fit)
    list_entry_t *le = &_free.free_list;
    PageDesc *page = PMM_INVALID_PTR;
    
    while ((le = list_next(le)) != &_free.free_list) {
        PageDesc *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    
    if (page) {
        // Split the block if it's larger than requested
        if (page->property > n) {
            PageDesc *remaining = page + n;
            remaining->property = page->property - n;
            SET_PAGE_RESERVED(remaining);
            list_add(le, &(remaining->page_link));
        }
        
        // Remove allocated block from free list
        list_del(le);
        _free.nr_free -= n;
        CLEAR_PAGE_RESERVED(page);
    }
    
    return page;
}

/**
 * @brief Free n contiguous pages and merge with adjacent free blocks
 * @param base Pointer to the first page descriptor to free
 * @param n Number of pages to free
 */
static void free(PageDesc *base, size_t n) {
    // Clear flags for all pages being freed
    PageDesc* p = base;
    for (; p != base + n; p++) {
        p->flags = PAGE_INIT_VALUE;
    }
    
    // Mark the block as free
    base->property = n;
    SET_PAGE_RESERVED(base);
    
    // Try to merge with adjacent free blocks
    list_entry_t *le = &_free.free_list;
    list_entry_t *prev = le;
    
    while ((le = list_next(le)) != &_free.free_list) {
        p = le2page(le, page_link);
        
        // Check if we can merge with the next block (base is before p)
        if (base + base->property == p) {
            base->property += p->property;
            CLEAR_PAGE_RESERVED(p);
            list_del(le);
        } 
        // Check if we can merge with the previous block (p is before base)
        else if (p + p->property == base) {
            p->property += base->property;
            CLEAR_PAGE_RESERVED(base);
            base = p;
            list_del(le);
        } 
        else {
            prev = le;
        }
    }
    
    // Add the merged block back to the free list
    _free.nr_free += n;
    list_add(prev, &(base->page_link));
}

/**
 * @brief Get the number of free pages
 * @return Number of free pages available
 */
static size_t nr_free_pages() {
    return _free.nr_free;
}

/**
 * @brief Self-check function to verify memory manager integrity
 */
static void check() {
    list_entry_t *le = &_free.free_list;
    size_t total_free = PAGE_INIT_VALUE;
    
    // Count total free pages
    while ((le = list_next(le)) != &_free.free_list) {
        PageDesc *p = le2page(le, page_link);
        assert(PAGE_RESERVED(p));
        total_free += p->property;
    }
    
    // Verify consistency
    assert(total_free == _free.nr_free);

    // Test allocation
    PageDesc* p0 = alloc(TEST_ALLOC_PAGES);
    assert(p0 != PMM_INVALID_PTR);
    assert(!PAGE_RESERVED(p0));
}

// Physical Memory Manager using First-Fit algorithm
const pmm_manager firstfit_pmm_mgr = {
    .name = "firstfit",
    .init = init,
    .init_memmap = init_memmap,
    .alloc = alloc,
    .free = free,
    .nr_free_pages = nr_free_pages,
    .check = check
};