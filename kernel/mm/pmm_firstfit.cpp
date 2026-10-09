#include "pmm.h"
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
// - Allocation: O(page_count) where page_count is the number of free blocks
// - Deallocation: O(page_count) for merging adjacent blocks
//
// Space Complexity: O(1) auxiliary space

#define PMM_SUCCESS      0
#define PMM_INVALID_PTR  nullptr
#define PAGE_INIT_VALUE  0
#define TEST_ALLOC_PAGES 5

FreeArea free_area{};

const char* PageAllocator::name() const {
    return "First-Fit Page Allocator";
}

void PageAllocator::init() {}

void PageAllocator::init_memmap(Page* base, size_t page_count) {
    for (Page* p = base; p != base + page_count; p++) {
        new (p) Page();
    }

    base->block_page_count = page_count;
    base->set_reserved();

    free_area.free_page_count += page_count;
    free_area.free_list.add_before(base->node());
}

Page* PageAllocator::alloc(size_t page_count) {
    if (page_count > free_area.free_page_count) {
        return nullptr;
    }

    Page* page{};

    ListNode* valid_node = &free_area.free_list;
    while ((valid_node = valid_node->next_node()) != &free_area.free_list) {
        Page* p = valid_node->container<Page>();
        if (p->block_page_count >= page_count) {
            page = p;
            break;
        }
    }

    if (page) {
        if (page->block_page_count > page_count) {
            Page* remaining = page + page_count;
            remaining->block_page_count = page->block_page_count - page_count;
            remaining->set_reserved();
            valid_node->add_after(remaining->node());
        }
        valid_node->unlink();
        free_area.free_page_count -= page_count;
        page->clear_reserved();
    }

    return page;
}

void PageAllocator::free(Page* base, size_t page_count) {
    for (Page* p = base; p != base + page_count; p++) {
        assert(p->ref_count == 0);
        p->ref_count = 0;
        p->flags = PAGE_INIT_VALUE;
    }

    base->block_page_count = page_count;
    base->set_reserved();

    ListNode* le = free_area.free_list.next_node();
    ListNode* prev = &free_area.free_list;

    while (le != &free_area.free_list) {
        ListNode* next = le->next_node();
        Page* p = le->container<Page>();

        if (base + base->block_page_count == p) {
            base->block_page_count += p->block_page_count;
            p->clear_reserved();
            le->unlink();
        }

        else if (p + p->block_page_count == base) {
            p->block_page_count += base->block_page_count;
            base->clear_reserved();
            base = p;
            le->unlink();
        } else {
            prev = le;
        }
        le = next;
    }

    free_area.free_page_count += page_count;
    prev->add_after(base->node());
}

size_t PageAllocator::free_page_count() const {
    return free_area.free_page_count;
}
