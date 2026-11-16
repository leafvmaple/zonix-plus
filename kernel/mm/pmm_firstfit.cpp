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
// - Allocation: O(n), where n is the number of free runs
// - Deallocation: O(page_count + n) to reset pages and merge free runs
//
// Space Complexity: O(1) auxiliary space

#define PMM_SUCCESS      0
#define PMM_INVALID_PTR  nullptr
#define PAGE_INIT_VALUE  0
#define TEST_ALLOC_PAGES 5

const char* PageAllocator::name() const {
    return "First-Fit Page Allocator";
}

void PageAllocator::init() {
    new (&free_area_) FreeArea();
}

void PageAllocator::init_memmap(Page* base, size_t page_count) {
    assert(base && page_count != 0);
    for (Page* p = base; p != base + page_count; p++) {
        new (p) Page();
    }

    base->block_page_count = page_count;
    base->set_reserved();

    free_area_.free_page_count += page_count;
    free_area_.free_list.push_back(*base);
}

Page* PageAllocator::alloc(size_t page_count) {
    if (page_count == 0 || page_count > free_area_.free_page_count) {
        return nullptr;
    }

    Page* page{};

    auto& free_list = free_area_.free_list;
    for (auto it = free_list.begin(); it != free_list.end(); ++it) {
        if (it->block_page_count < page_count) {
            continue;
        }
        page = &*it;
        if (page->block_page_count > page_count) {
            Page* remaining = page + page_count;
            remaining->block_page_count = page->block_page_count - page_count;
            remaining->set_reserved();
            auto following = it;
            free_list.insert(++following, *remaining);
        }
        free_list.erase(it);
        free_area_.free_page_count -= page_count;
        page->clear_reserved();
        page->block_page_count = page_count;
        break;
    }

    return page;
}

void PageAllocator::free(Page* base, size_t page_count) {
    assert(base && page_count != 0);
    for (Page* p = base; p != base + page_count; p++) {
        assert(p->ref_count == 0);
        p->ref_count = 0;
        p->flags = PAGE_INIT_VALUE;
        p->block_page_count = 0;
    }

    base->block_page_count = page_count;
    base->set_reserved();

    auto& free_list = free_area_.free_list;
    Page* previous = nullptr;
    for (auto it = free_list.begin(); it != free_list.end();) {
        Page* page = &*it;
        if (base + base->block_page_count == page) {
            base->block_page_count += page->block_page_count;
            page->clear_reserved();
            it = free_list.erase(it);
        } else if (page + page->block_page_count == base) {
            page->block_page_count += base->block_page_count;
            base->clear_reserved();
            base = page;
            it = free_list.erase(it);
        } else {
            previous = page;
            ++it;
        }
    }

    free_area_.free_page_count += page_count;
    auto insertion = free_list.begin();
    if (previous) {
        insertion = free_list.iterator_to(*previous);
        ++insertion;
    }
    free_list.insert(insertion, *base);
}

size_t PageAllocator::free_page_count() const {
    return free_area_.free_page_count;
}
