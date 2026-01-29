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
#define PMM_INVALID_PTR   nullptr
#define PAGE_INIT_VALUE   0
#define TEST_ALLOC_PAGES  5

FirstFitPMMManager::FirstFitPMMManager() {
    m_name = "First-Fit PMM Manager";
}

void FirstFitPMMManager::init() {
}

void FirstFitPMMManager::init_memmap(Page *base, size_t n) {
    for (Page* p = base; p != base + n; p++) {
        p->ref = PAGE_INIT_VALUE;
        p->flags = PAGE_INIT_VALUE;
        p->property = PAGE_INIT_VALUE;
    }

    base->property = n;
    SET_PAGE_RESERVED(base);
    
    // Add to free list
    m_free.nr_free += n;
    list_add_before(&m_free.free_list, &(base->page_link));
}

Page* FirstFitPMMManager::alloc(size_t n) {
    if (n > m_free.nr_free) {
        return PMM_INVALID_PTR;
    }

    list_entry_t *le = &m_free.free_list;
    Page *page = PMM_INVALID_PTR;
    
    while ((le = list_next(le)) != &m_free.free_list) {
        Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    
    if (page) {
        if (page->property > n) {
            Page *remaining = page + n;
            remaining->property = page->property - n;
            SET_PAGE_RESERVED(remaining);
            list_add(le, &(remaining->page_link));
        }
        list_del(le);
        m_free.nr_free -= n;
        CLEAR_PAGE_RESERVED(page);
    }
    
    return page;
}

void FirstFitPMMManager::free(Page *base, size_t n) {
    Page* p = base;
    for (; p != base + n; p++) {
        p->flags = PAGE_INIT_VALUE;
    }

    base->property = n;
    SET_PAGE_RESERVED(base);

    list_entry_t *le = &m_free.free_list;
    list_entry_t *prev = le;
    
    while ((le = list_next(le)) != &m_free.free_list) {
        p = le2page(le, page_link);

        if (base + base->property == p) {
            base->property += p->property;
            CLEAR_PAGE_RESERVED(p);
            list_del(le);
        } 

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

    m_free.nr_free += n;
    list_add(prev, &(base->page_link));
}

size_t FirstFitPMMManager::nr_free_pages() {
    return m_free.nr_free;
}

void FirstFitPMMManager::check() {
    list_entry_t *le = &m_free.free_list;
    size_t total_free = PAGE_INIT_VALUE;
    
    // Count total free pages
    while ((le = list_next(le)) != &m_free.free_list) {
        Page *p = le2page(le, page_link);
        assert(PAGE_RESERVED(p));
        total_free += p->property;
    }
    
    // Verify consistency
    assert(total_free == m_free.nr_free);

    // Test allocation
    Page* p0 = alloc(TEST_ALLOC_PAGES);
    assert(p0 != PMM_INVALID_PTR);
    assert(!PAGE_RESERVED(p0));
}
