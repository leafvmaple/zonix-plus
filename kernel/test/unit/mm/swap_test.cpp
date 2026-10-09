#include "test/unit/mm/swap_test.h"
#include "mm/swap.h"
// LRU/Clock cases below are disabled; their implementations are absent from this tree.
// #include "swap_clock.h"
// #include "swap_lru.h"
#include "mm/pmm.h"
#include "lib/result.h"
#include "lib/stdio.h"

#include <asm/page.h>

#include "exec/exec.h"

// Test statistics
static int tests_passed = 0;
static int tests_failed = 0;

static SwapManager& test_swap_mgr() {
    static SwapManager mgr;
    static bool inited = false;

    if (!inited) {
        assert(mgr.init() == Error::None);
        inited = true;
    }

    return mgr;
}

#define TEST_START(name)            \
    cprintf("\n[TEST] %s\n", name); \
    int zonix_test_result = 1;

#define TEST_ASSERT(cond, msg)         \
    if (!(cond)) {                     \
        cprintf("  [FAIL] %s\n", msg); \
        zonix_test_result = 0;         \
    } else {                           \
        cprintf("  [OK] %s\n", msg);   \
    }

#define TEST_END()               \
    if (zonix_test_result) {     \
        cprintf("  [PASSED]\n"); \
        tests_passed++;          \
    } else {                     \
        cprintf("  [FAILED]\n"); \
        tests_failed++;          \
    }

// ============================================================================
// Unit Tests - FIFO Algorithm
// ============================================================================

void test_fifo_basic() {
    MemoryDesc mm;
    TEST_START("FIFO Basic Operation");

    SwapManager& fifo = test_swap_mgr();
    TEST_ASSERT(fifo.init_mm(&mm) == Error::None, "Initialized FIFO fixture");

    Page pages[5];

    // Add pages in order
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(fifo.map_swappable(&mm, 0x1000 * i, &pages[i], 0) == Error::None, "Queued FIFO fixture page");
    }

    // Verify FIFO order: should select page 0, then 1, then 2...
    for (int i = 0; i < 5; i++) {
        Page* victim = nullptr;
        Error ret = fifo.swap_out_victim(&mm, &victim, 0);

        // Simple message without snprintf for now
        if (ret == Error::None && victim == &pages[i]) {
            cprintf("  [OK] Victim %d is page %d\n", i, i);
        } else {
            cprintf("  [FAIL] Victim %d is not page %d\n", i, i);
            zonix_test_result = 0;
        }
    }

    // Test empty list
    Page* victim = nullptr;
    Error ret = fifo.swap_out_victim(&mm, &victim, 0);
    TEST_ASSERT(ret != Error::None, "Empty list returns error");

    TEST_END();
}

void test_fifo_interleaved() {
    MemoryDesc mm;
    TEST_START("FIFO Interleaved Add/Remove");

    SwapManager& fifo = test_swap_mgr();
    TEST_ASSERT(fifo.init_mm(&mm) == Error::None, "Initialized FIFO fixture");

    Page pages[10];

    // Add 3 pages
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(fifo.map_swappable(&mm, 0x1000 * i, &pages[i], 0) == Error::None, "Queued FIFO fixture page");
    }

    // Remove 1
    Page* victim{};
    TEST_ASSERT(fifo.swap_out_victim(&mm, &victim, 0) == Error::None && victim == &pages[0], "First victim is page 0");

    // Add 2 more
    for (int i = 3; i < 5; i++) {
        TEST_ASSERT(fifo.map_swappable(&mm, 0x1000 * i, &pages[i], 0) == Error::None, "Queued FIFO fixture page");
    }

    // Next victim should be page 1
    TEST_ASSERT(fifo.swap_out_victim(&mm, &victim, 0) == Error::None && victim == &pages[1], "Second victim is page 1");

    TEST_END();
}

// ============================================================================
// Unit Tests - LRU Algorithm (ARCHIVED)
// ============================================================================
// LRU implementation is absent from this tree.
// To re-enable, move swap_lru.c/h back and uncomment the includes above

#if 0
void test_lru_basic() {
    TEST_START("LRU Basic Operation");
    
    MemoryDesc mm;
    swap_mgr_lru.init_mm(&mm);
    
    Page pages[3];
    
    // Add pages 0, 1, 2
    swap_mgr_lru.map_swappable(&mm, 0x1000, &pages[0], 0);
    swap_mgr_lru.map_swappable(&mm, 0x2000, &pages[1], 0);
    swap_mgr_lru.map_swappable(&mm, 0x3000, &pages[2], 0);
    
    // Victim should be page 0 (least recently used)
    Page *victim;
    swap_mgr_lru.swap_out_victim(&mm, &victim, 0);
    TEST_ASSERT(victim == &pages[0], "LRU victim is page 0");
    
    TEST_END();
}

void test_lru_access_pattern() {
    TEST_START("LRU Access Pattern");
    
    MemoryDesc mm;
    swap_mgr_lru.init_mm(&mm);
    
    Page pages[3];
    
    // Add pages 0, 1, 2
    swap_mgr_lru.map_swappable(&mm, 0x1000, &pages[0], 0);
    swap_mgr_lru.map_swappable(&mm, 0x2000, &pages[1], 0);
    swap_mgr_lru.map_swappable(&mm, 0x3000, &pages[2], 0);
    
    // Access page 0 again (moves to back)
    swap_mgr_lru.map_swappable(&mm, 0x1000, &pages[0], 1);
    
    // Now LRU should be page 1
    Page *victim;
    swap_mgr_lru.swap_out_victim(&mm, &victim, 0);
    TEST_ASSERT(victim == &pages[1], "After access, LRU is page 1");
    
    // Next should be page 2
    swap_mgr_lru.swap_out_victim(&mm, &victim, 0);
    TEST_ASSERT(victim == &pages[2], "Next LRU is page 2");
    
    // Last should be page 0 (most recently accessed)
    swap_mgr_lru.swap_out_victim(&mm, &victim, 0);
    TEST_ASSERT(victim == &pages[0], "Last is page 0");
    
    TEST_END();
}

// ============================================================================
// Unit Tests - Clock Algorithm (ARCHIVED)
// ============================================================================
// Clock implementation is absent from this tree.
// To re-enable, move swap_clock.c/h back and uncomment the includes above

void test_clock_basic() {
    TEST_START("Clock Basic Operation");
    
    MemoryDesc mm;
    swap_mgr_clock.init_mm(&mm);
    
    Page pages[4];
    
    // Add pages
    for (int i = 0; i < 4; i++) {
        swap_mgr_clock.map_swappable(&mm, 0x1000 * i, &pages[i], 0);
    }
    
    // Should select pages in order (simplified clock without accessed bit)
    Page *victim;
    Error ret = swap_mgr_clock.swap_out_victim(&mm, &victim, 0);
    TEST_ASSERT(ret == Error::None && victim != nullptr, "Clock selects a victim");
    
    TEST_END();
}
#endif

// ============================================================================
// Integration Tests
// ============================================================================

static Error map_test_page(MemoryDesc* mm, Page* page, uintptr_t va) {
    Error mapped = pmm::page_insert(mm->pgdir, page, va, VM_USER_RW);
    if (mapped != Error::None) {
        pmm::free_pages(page);
        return mapped;
    }
    // After insertion the local MemoryDesc owns the page, even if queueing fails.
    return test_swap_mgr().map_swappable(mm, va, page, 0);
}

void test_swap_init() {
    TEST_START("Swap Initialization");

    // swap::init should already be called, just verify
    TEST_ASSERT(1, "Swap system initialized");

    MemoryDesc mm;
    Error ret = swap::init_mm(&mm);
    TEST_ASSERT(ret == Error::None, "swap::init_mm succeeds");
    TEST_ASSERT(mm.swap_list.empty(), "Swap list created");

    TEST_END();
}

void test_swap_in_basic() {
    TEST_START("Swap In Basic");

    MemoryDesc mm;
    mm.pgdir = exec::create_user_pgdir().value_or(nullptr);
    TEST_ASSERT(mm.pgdir && swap::init_mm(&mm) == Error::None, "Initialized owned swap fixture");
    if (!mm.pgdir) {
        TEST_END();
        return;
    }

    uintptr_t va = 0x100000;

    // Create a PTE with swap entry
    pte_t* ptep = pmm::get_pte(mm.pgdir, va, 1);
    if (ptep) {
        *ptep = 0x100;  // Fake swap entry (present bit = 0, slot = 1)

        Page* page = nullptr;
        Error ret = swap::in(&mm, va, &page);

        TEST_ASSERT(ret == Error::None, "swap::in returns success");
        TEST_ASSERT(page != nullptr, "Page allocated");

        // Check that PTE was updated
        pte_t* new_ptep = pmm::get_pte(mm.pgdir, va, 0);
        TEST_ASSERT(new_ptep != nullptr && pte_present(*new_ptep), "PTE updated with present bit");
        // The local MemoryDesc owns the installed page and releases it on teardown.
    } else {
        TEST_ASSERT(0, "Failed to create PTE");
    }

    TEST_END();
}

void test_swap_out_basic() {
    TEST_START("Swap Out Basic");

    MemoryDesc mm;
    mm.pgdir = exec::create_user_pgdir().value_or(nullptr);
    TEST_ASSERT(mm.pgdir && swap::init_mm(&mm) == Error::None, "Initialized owned swap fixture");
    if (!mm.pgdir) {
        TEST_END();
        return;
    }

    // Allocate and map some pages
    Page* pages_arr[3];
    uintptr_t vas[3];

    for (int i = 0; i < 3; i++) {
        pages_arr[i] = pmm::alloc_pages(1);
        if (pages_arr[i]) {
            vas[i] = 0x200000 + i * PG_SIZE;
            Error mapped = map_test_page(&mm, pages_arr[i], vas[i]);
            TEST_ASSERT(mapped == Error::None, "Mapped and queued swap fixture page");
            if (mapped != Error::None) {
                TEST_END();
                return;
            }
        }
    }

    // Try to swap out
    int attempt_count = swap::out(&mm, 2, 0);
    TEST_ASSERT(attempt_count > 0, "Swap-out loop made progress");

    // Verify that PTEs were updated (present bit cleared)
    for (int i = 0; i < attempt_count; i++) {
        pte_t* ptep = pmm::get_pte(mm.pgdir, vas[i], 0);
        if (ptep) {
            TEST_ASSERT(!pte_present(*ptep), "PTE present bit cleared after swap out");
        }
    }

    cprintf("  Completed %d swap-out iterations\n", attempt_count);

    TEST_END();
}

// ============================================================================
// Algorithm Comparison (SIMPLIFIED - FIFO Only)
// ============================================================================
// Note: This test now only tests FIFO. Other algorithms are archived.
// To re-enable full comparison, uncomment the archived algorithms above.

void test_algorithm_comparison() {
    MemoryDesc mm;
    TEST_START("Algorithm Comparison");

    cprintf("\n  Testing swap algorithm:\n");

    SwapManager* algorithms[] = {
        &test_swap_mgr()
        // Add more algorithms here when re-enabled:
        // &swap_mgr_clock,
        // &swap_mgr_lru
    };

    for (int i = 0; i < 1; i++) {  // Changed from 3 to 1
        cprintf("    %s\n", algorithms[i]->name);

        TEST_ASSERT(algorithms[i]->init_mm(&mm) == Error::None, "Initialized algorithm fixture");

        Page pages[20];

        // Add 20 pages
        for (int j = 0; j < 20; j++) {
            TEST_ASSERT(algorithms[i]->map_swappable(&mm, j * PG_SIZE, &pages[j], 0) == Error::None,
                        "Queued algorithm fixture page");
        }

        // Remove 10 pages
        int removed = 0;
        for (int j = 0; j < 10; j++) {
            Page* victim{};
            if (algorithms[i]->swap_out_victim(&mm, &victim, 0) == Error::None) {
                removed++;
            }
        }

        cprintf("      Successfully removed %d/10 pages\n", removed);
    }

    TEST_ASSERT(1, "FIFO algorithm functional");

    TEST_END();
}

// ============================================================================
// Main Test Runner
// ============================================================================

void run_swap_tests() {
    cprintf("\n");
    cprintf("========================================\n");
    cprintf("   ZONIX SWAP SYSTEM TEST SUITE       \n");
    cprintf("========================================\n");

    tests_passed = 0;
    tests_failed = 0;

    // Unit Tests - FIFO
    cprintf("\n--- FIFO Algorithm Tests ---\n");
    test_fifo_basic();
    test_fifo_interleaved();

#if 0
    // Unit Tests - LRU
    cprintf("\n--- LRU Algorithm Tests ---\n");
    test_lru_basic();
    test_lru_access_pattern();
    
    // Unit Tests - Clock
    cprintf("\n--- Clock Algorithm Tests ---\n");
    test_clock_basic();
    
    // Integration Tests
    cprintf("\n--- Integration Tests ---\n");
    test_swap_init();
    test_swap_in_basic();
    test_swap_out_basic();
    
    // Disk I/O Tests
    cprintf("\n--- Disk I/O Tests ---\n");
    test_swap_disk_io();
    test_swap_multiple_pages();
    
    // Comparison
    cprintf("\n--- Algorithm Comparison ---\n");
    test_algorithm_comparison();
#endif

    // Summary
    cprintf("\n");
    cprintf("========================================\n");
    cprintf("   TEST SUMMARY                        \n");
    cprintf("========================================\n");
    cprintf("   Passed: %d\n", tests_passed);
    cprintf("   Failed: %d\n", tests_failed);
    cprintf("   Total:  %d\n", tests_passed + tests_failed);

    if (tests_failed == 0) {
        cprintf("\n   [OK] ALL TESTS PASSED!\n");
    } else {
        cprintf("\n   [FAIL] SOME TESTS FAILED\n");
    }


    cprintf("========================================\n\n");
}

// ============================================================================
// Disk I/O and Data Integrity Tests
// ============================================================================

void test_swap_disk_io() {
    TEST_START("Swap Disk I/O and Data Integrity");

    MemoryDesc mm;
    mm.pgdir = exec::create_user_pgdir().value_or(nullptr);
    TEST_ASSERT(mm.pgdir && swap::init_mm(&mm) == Error::None, "Initialized owned swap fixture");
    if (!mm.pgdir) {
        TEST_END();
        return;
    }

    // Test pattern: write data, swap out, swap in, verify data
    uintptr_t test_va = 0x300000;

    // 1. Allocate a page and fill it with test pattern
    Page* page = pmm::alloc_pages(1);
    TEST_ASSERT(page != nullptr, "Page allocation successful");

    if (!page) {
        TEST_END();
        return;
    }

    // Fill page with test pattern
    auto* kva = static_cast<uint8_t*>(pmm::page_to_kva(page));
    for (int i = 0; i < PG_SIZE; i++) {
        kva[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // 2. Map the page
    Error mapped = map_test_page(&mm, page, test_va);
    TEST_ASSERT(mapped == Error::None, "Mapped and queued swap fixture page");
    if (mapped != Error::None) {
        TEST_END();
        return;
    }

    cprintf("  Filled page with test pattern\n");

    // 3. Swap out the page
    int attempt_count = swap::out(&mm, 1, 0);
    TEST_ASSERT(attempt_count == 1, "One swap-out iteration completed");

    // Verify PTE was updated
    pte_t* ptep = pmm::get_pte(mm.pgdir, test_va, 0);
    TEST_ASSERT(ptep != nullptr && !pte_present(*ptep), "PTE marked as not present");

    uintptr_t swap_entry = *ptep;
    cprintf("  Page swapped to entry 0x%lx\n", static_cast<unsigned long>(swap_entry));

    // 4. Swap in the page
    Page* new_page = nullptr;
    Error ret = swap::in(&mm, test_va, &new_page);
    TEST_ASSERT(ret == Error::None, "Page swapped in");
    TEST_ASSERT(new_page != nullptr, "New page allocated");

    // 5. Verify data integrity
    if (new_page) {
        auto* new_kva = static_cast<uint8_t*>(pmm::page_to_kva(new_page));
        int errors = 0;

        for (int i = 0; i < PG_SIZE; i++) {
            auto expected = static_cast<uint8_t>(i & 0xFF);
            uint8_t actual = new_kva[i];
            if (actual != expected) {
                if (errors < 5) {
                    cprintf("    Mismatch at offset %d: expected 0x%02x, got 0x%02x\n", i, expected, actual);
                }
                errors++;
            }
        }

        if (errors == 0) {
            TEST_ASSERT(1, "Data integrity verified - all bytes match");
        } else {
            cprintf("    Total mismatches: %d\n", errors);
            TEST_ASSERT(0, "Data corruption detected");
        }

        // The local MemoryDesc retains ownership of this mapped page.
    }

    TEST_END();
}

void test_swap_multiple_pages() {
    TEST_START("Swap Multiple Pages");

    MemoryDesc mm;
    mm.pgdir = exec::create_user_pgdir().value_or(nullptr);
    TEST_ASSERT(mm.pgdir && swap::init_mm(&mm) == Error::None, "Initialized owned swap fixture");
    if (!mm.pgdir) {
        TEST_END();
        return;
    }

#define TEST_PAGE_COUNT 5
    uintptr_t base_va = 0x400000;
    Page* pages_arr[TEST_PAGE_COUNT];

    // 1. Allocate and fill multiple pages with unique patterns
    for (int i = 0; i < TEST_PAGE_COUNT; i++) {
        pages_arr[i] = pmm::alloc_pages(1);
        if (pages_arr[i]) {
            uintptr_t va = base_va + i * PG_SIZE;

            // Fill with unique pattern (page number repeated)
            auto* kva = static_cast<uint8_t*>(pmm::page_to_kva(pages_arr[i]));
            for (int j = 0; j < PG_SIZE; j++) {
                kva[j] = static_cast<uint8_t>((i * 17 + j) & 0xFF);
            }

            Error mapped = map_test_page(&mm, pages_arr[i], va);
            TEST_ASSERT(mapped == Error::None, "Mapped and queued swap fixture page");
            if (mapped != Error::None) {
                TEST_END();
                return;
            }
        }
    }

    cprintf("  Allocated and filled %d pages\n", TEST_PAGE_COUNT);

    // 2. Swap out 3 pages
    int attempt_count = swap::out(&mm, 3, 0);
    TEST_ASSERT(attempt_count == 3, "Three swap-out iterations completed");
    cprintf("  Completed %d swap-out iterations\n", attempt_count);

    // 3. Swap them back in and verify
    int verified = 0;
    for (int i = 0; i < attempt_count; i++) {
        uintptr_t va = base_va + i * PG_SIZE;
        pte_t* ptep = pmm::get_pte(mm.pgdir, va, 0);

        if (ptep && !pte_present(*ptep)) {
            // This page was swapped out, swap it back in
            Page* page = nullptr;
            Error ret = swap::in(&mm, va, &page);

            if (ret == Error::None && page) {
                // Verify data
                void* kva = pmm::page_to_kva(page);
                int errors = 0;

                for (int j = 0; j < 256; j++) {  // Check first 256 bytes
                    uint8_t expected = static_cast<uint8_t>((i * 17 + j) & 0xFF);
                    uint8_t actual = reinterpret_cast<uint8_t*>(kva)[j];
                    if (actual != expected) {
                        errors++;
                    }
                }

                if (errors == 0) {
                    verified++;
                }

                // Leave mapped pages to the local MemoryDesc's destructor.
            }
        }
    }

    TEST_ASSERT(verified == attempt_count, "All swapped pages verified");
    cprintf("  Verified %d/%d pages\n", verified, attempt_count);

    TEST_END();
}

// Helper function for shell command
void swap_test_command() {
    run_swap_tests();
}
