#include "test/unit/mm/swap_test.h"
#include "mm/swap.h"
#include "lib/stdio.h"

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
    mm.swap_list.clear();  // Detach stack fixtures before their lifetimes end.

    TEST_END();
}

void run_swap_tests() {
    cprintf("\n");
    cprintf("========================================\n");
    cprintf("   ZONIX SWAP SYSTEM TEST SUITE       \n");
    cprintf("========================================\n");

    tests_passed = 0;
    tests_failed = 0;

    cprintf("\n--- FIFO Algorithm Tests ---\n");
    test_fifo_basic();
    test_fifo_interleaved();

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

void swap_test_command() {
    run_swap_tests();
}
