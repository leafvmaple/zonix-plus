#include "pmm_firstfit.h"
#include "debug/assert.h"

#include "lib/memory.h"

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

#define PMM_SUCCESS      0
#define PMM_INVALID_PTR  nullptr
#define PAGE_INIT_VALUE  0
#define TEST_ALLOC_PAGES 5

FirstFitPageAllocator::FirstFitPageAllocator() {
    name_ = "First-Fit Page Allocator";
}

void FirstFitPageAllocator::init() {}

void FirstFitPageAllocator::init_memmap(Page* base, size_t n) {
    for (Page* p = base; p != base + n; p++) {
        new (p) Page();
    }

    base->property = n;
    base->set_reserved();

    free_.nr_free += n;
    free_.free_list.add_before(base->node());
}

Page* FirstFitPageAllocator::alloc(size_t n) {
    if (n > free_.nr_free) {
        return nullptr;
    }

    Page* page{};

    ListNode* valid_node = &free_.free_list;
    while ((valid_node = valid_node->get_next()) != &free_.free_list) {
        Page* p = valid_node->container<Page>();
        if (p->property >= n) {
            page = p;
            break;
        }
    }

    if (page) {
        if (page->property > n) {
            Page* remaining = page + n;
            remaining->property = page->property - n;
            remaining->set_reserved();
            valid_node->add_after(remaining->node());
        }
        valid_node->unlink();
        free_.nr_free -= n;
        page->clear_reserved();
    }

    return page;
}

void FirstFitPageAllocator::free(Page* base, size_t n) {
    for (Page* p = base; p != base + n; p++) {
        p->flags = PAGE_INIT_VALUE;
    }

    base->property = n;
    base->set_reserved();

    ListNode* le = &free_.free_list;
    ListNode* prev = le;

    while ((le = le->get_next()) != &free_.free_list) {
        Page* p = le->container<Page>();

        if (base + base->property == p) {
            base->property += p->property;
            p->clear_reserved();
            le->unlink();
        }

        else if (p + p->property == base) {
            p->property += base->property;
            base->clear_reserved();
            base = p;
            le->unlink();
        } else {
            prev = le;
        }
    }

    free_.nr_free += n;
    prev->add_after(base->node());
}

size_t FirstFitPageAllocator::nr_free_pages() {
    return free_.nr_free;
}

void FirstFitPageAllocator::check() {
    size_t total_free{};

    // Count total free pages
    ListNode* le = &free_.free_list;
    while ((le = le->get_next()) != &free_.free_list) {
        Page* p = le->container<Page>();
        assert(p->is_reserved());
        total_free += p->property;
    }

    // Verify consistency
    assert(total_free == free_.nr_free);

    // Test allocation
    Page* p0 = alloc(TEST_ALLOC_PAGES);
    assert(p0 != PMM_INVALID_PTR);
    assert(!p0->is_reserved());
}
