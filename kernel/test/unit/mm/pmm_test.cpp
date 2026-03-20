#include "test/test_defs.h"
#include "mm/pmm.h"
#include "lib/memory.h"

#include <asm/page.h>

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Basic page allocation and free
// ============================================================================

static void test_alloc_free_single() {
    TEST_START("PMM alloc/free single page");

    Page* page = pmm::alloc_pages(1);
    TEST_ASSERT(page != nullptr, "Single page allocation succeeds");

    if (page) {
        uintptr_t pa = pmm::page_to_phys(page);
        TEST_ASSERT((pa & (PG_SIZE - 1)) == 0, "Physical address is page-aligned");
        TEST_ASSERT(page->ref == 0, "Newly allocated page ref count is 0");

        void* kva = pmm::page_to_kva(page);
        TEST_ASSERT(kva != nullptr, "page_to_kva returns valid address");

        Page* roundtrip = pmm::kva_to_page(kva);
        TEST_ASSERT(roundtrip == page, "kva_to_page roundtrip is consistent");

        Page* roundtrip2 = pmm::phys_to_page(pa);
        TEST_ASSERT(roundtrip2 == page, "phys_to_page roundtrip is consistent");

        pmm::free_pages(page, 1);
    }

    TEST_END();
}

// ============================================================================
// Multiple page allocation
// ============================================================================

static void test_alloc_multiple() {
    TEST_START("PMM alloc multiple pages");

    Page* pages = pmm::alloc_pages(4);
    TEST_ASSERT(pages != nullptr, "Allocate 4 contiguous pages");

    if (pages) {
        uintptr_t pa_base = pmm::page_to_phys(pages);
        TEST_ASSERT((pa_base & (PG_SIZE - 1)) == 0, "Base is page-aligned");

        pmm::free_pages(pages, 4);
    }

    TEST_END();
}

// ============================================================================
// Alloc-free-alloc reuse
// ============================================================================

static void test_alloc_free_reuse() {
    TEST_START("PMM alloc-free-alloc reuse");

    Page* p1 = pmm::alloc_pages(1);
    TEST_ASSERT(p1 != nullptr, "First allocation succeeds");

    if (p1) {
        uintptr_t pa1 = pmm::page_to_phys(p1);
        pmm::free_pages(p1, 1);

        Page* p2 = pmm::alloc_pages(1);
        TEST_ASSERT(p2 != nullptr, "Second allocation after free succeeds");

        if (p2) {
            pmm::free_pages(p2, 1);
        }
    }

    TEST_END();
}

// ============================================================================
// Page content read/write
// ============================================================================

static void test_page_readwrite() {
    TEST_START("PMM page content read/write");

    Page* page = pmm::alloc_pages(1);
    TEST_ASSERT(page != nullptr, "Allocation succeeds");

    if (page) {
        auto* kva = static_cast<uint8_t*>(pmm::page_to_kva(page));

        for (int i = 0; i < PG_SIZE; i++) {
            kva[i] = static_cast<uint8_t>(i & 0xFF);
        }

        bool match = true;
        for (int i = 0; i < PG_SIZE; i++) {
            if (kva[i] != static_cast<uint8_t>(i & 0xFF)) {
                match = false;
                break;
            }
        }
        TEST_ASSERT(match, "Written data reads back correctly");

        pmm::free_pages(page, 1);
    }

    TEST_END();
}

// ============================================================================
// kmalloc / kfree
// ============================================================================

static void test_kmalloc_kfree() {
    TEST_START("kmalloc / kfree");

    void* ptr = kmalloc(128);
    TEST_ASSERT(ptr != nullptr, "kmalloc(128) returns non-null");

    if (ptr) {
        memset(ptr, 0x42, 128);
        auto* bp = static_cast<uint8_t*>(ptr);
        TEST_ASSERT(bp[0] == 0x42 && bp[127] == 0x42, "kmalloc memory is writable");
        kfree(ptr);
    }

    kfree(nullptr);
    TEST_ASSERT(true, "kfree(nullptr) does not crash");

    TEST_END();
}

// ============================================================================
// Stress: many small allocations
// ============================================================================

static void test_stress_alloc() {
    TEST_START("PMM stress: multiple allocations");

    constexpr int N = 16;
    Page* pages[N];
    int allocated = 0;

    for (int i = 0; i < N; i++) {
        pages[i] = pmm::alloc_pages(1);
        if (pages[i])
            allocated++;
    }

    TEST_ASSERT(allocated == N, "All 16 single-page allocations succeed");

    bool all_distinct = true;
    for (int i = 0; i < allocated && all_distinct; i++) {
        for (int j = i + 1; j < allocated; j++) {
            if (pages[i] == pages[j]) {
                all_distinct = false;
                break;
            }
        }
    }
    TEST_ASSERT(all_distinct, "All allocated pages are distinct");

    for (int i = 0; i < allocated; i++) {
        pmm::free_pages(pages[i], 1);
    }

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace pmm_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_alloc_free_single();
    test_alloc_multiple();
    test_alloc_free_reuse();
    test_page_readwrite();
    test_kmalloc_kfree();
    test_stress_alloc();

    TEST_SUMMARY("PMM Allocator");
}

}  // namespace pmm_test
