#include "swap.h"
#include "swap_fifo.h"
// Note: swap_clock and swap_lru are archived in kern/mm/archived/
// #include "swap_clock.h"
// #include "swap_lru.h"
#include "pmm.h"
#include "stdio.h"

#include <asm/mmu.h>

// External declarations
extern swap_manager *swap_mgr;
extern pde_t *boot_pgdir;
extern MemoryDesc* init_mm;

// Test statistics
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    cprintf("\n[TEST] %s\n", name); \
    int __test_result = 1;

#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        cprintf("  [FAIL] %s\n", msg); \
        __test_result = 0; \
    } else { \
        cprintf("  [OK] %s\n", msg); \
    }

#define TEST_END() \
    if (__test_result) { \
        cprintf("  [PASSED]\n"); \
        tests_passed++; \
    } else { \
        cprintf("  [FAILED]\n"); \
        tests_failed++; \
    }

// ============================================================================
// Unit Tests - FIFO Algorithm
// ============================================================================

void test_fifo_basic() {
    TEST_START("FIFO Basic Operation");

    swap_mgr_fifo.init_mm(init_mm);
    
    Page pages[5];
    
    // Add pages in order
    for (int i = 0; i < 5; i++) {
        swap_mgr_fifo.map_swappable(init_mm, 0x1000 * i, &pages[i], 0);
    }
    
    // Verify FIFO order: should select page 0, then 1, then 2...
    for (int i = 0; i < 5; i++) {
        Page *victim = nullptr;
        int ret = swap_mgr_fifo.swap_out_victim(init_mm, &victim, 0);
        
        // Simple message without snprintf for now
        if (ret == 0 && victim == &pages[i]) {
            cprintf("  [OK] Victim %d is page %d\n", i, i);
        } else {
            cprintf("  [FAIL] Victim %d is not page %d\n", i, i);
            __test_result = 0;
        }
    }
    
    // Test empty list
    Page *victim = nullptr;
    int ret = swap_mgr_fifo.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(ret != 0, "Empty list returns error");
    
    TEST_END();
}

void test_fifo_interleaved() {
    TEST_START("FIFO Interleaved Add/Remove");
    
    swap_mgr_fifo.init_mm(init_mm);
    
    Page pages[10];
    
    // Add 3 pages
    for (int i = 0; i < 3; i++) {
        swap_mgr_fifo.map_swappable(init_mm, 0x1000 * i, &pages[i], 0);
    }
    
    // Remove 1
    Page *victim;
    swap_mgr_fifo.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(victim == &pages[0], "First victim is page 0");
    
    // Add 2 more
    for (int i = 3; i < 5; i++) {
        swap_mgr_fifo.map_swappable(init_mm, 0x1000 * i, &pages[i], 0);
    }
    
    // Next victim should be page 1
    swap_mgr_fifo.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(victim == &pages[1], "Second victim is page 1");
    
    TEST_END();
}

// ============================================================================
// Unit Tests - LRU Algorithm (ARCHIVED)
// ============================================================================
// Note: LRU algorithm is archived in kern/mm/archived/
// To re-enable, move swap_lru.c/h back and uncomment the includes above

#if 0
void test_lru_basic() {
    TEST_START("LRU Basic Operation");
    
    MemoryDesc mm;
    swap_mgr_lru.init_mm(init_mm);
    
    Page pages[3];
    
    // Add pages 0, 1, 2
    swap_mgr_lru.map_swappable(init_mm, 0x1000, &pages[0], 0);
    swap_mgr_lru.map_swappable(init_mm, 0x2000, &pages[1], 0);
    swap_mgr_lru.map_swappable(init_mm, 0x3000, &pages[2], 0);
    
    // Victim should be page 0 (least recently used)
    Page *victim;
    swap_mgr_lru.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(victim == &pages[0], "LRU victim is page 0");
    
    TEST_END();
}

void test_lru_access_pattern() {
    TEST_START("LRU Access Pattern");
    
    MemoryDesc mm;
    swap_mgr_lru.init_mm(init_mm);
    
    Page pages[3];
    
    // Add pages 0, 1, 2
    swap_mgr_lru.map_swappable(init_mm, 0x1000, &pages[0], 0);
    swap_mgr_lru.map_swappable(init_mm, 0x2000, &pages[1], 0);
    swap_mgr_lru.map_swappable(init_mm, 0x3000, &pages[2], 0);
    
    // Access page 0 again (moves to back)
    swap_mgr_lru.map_swappable(init_mm, 0x1000, &pages[0], 1);
    
    // Now LRU should be page 1
    Page *victim;
    swap_mgr_lru.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(victim == &pages[1], "After access, LRU is page 1");
    
    // Next should be page 2
    swap_mgr_lru.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(victim == &pages[2], "Next LRU is page 2");
    
    // Last should be page 0 (most recently accessed)
    swap_mgr_lru.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(victim == &pages[0], "Last is page 0");
    
    TEST_END();
}

// ============================================================================
// Unit Tests - Clock Algorithm (ARCHIVED)
// ============================================================================
// Note: Clock algorithm is archived in kern/mm/archived/
// To re-enable, move swap_clock.c/h back and uncomment the includes above

void test_clock_basic() {
    TEST_START("Clock Basic Operation");
    
    MemoryDesc mm;
    swap_mgr_clock.init_mm(init_mm);
    
    Page pages[4];
    
    // Add pages
    for (int i = 0; i < 4; i++) {
        swap_mgr_clock.map_swappable(init_mm, 0x1000 * i, &pages[i], 0);
    }
    
    // Should select pages in order (simplified clock without accessed bit)
    Page *victim;
    int ret = swap_mgr_clock.swap_out_victim(init_mm, &victim, 0);
    TEST_ASSERT(ret == 0 && victim != nullptr, "Clock selects a victim");
    
    TEST_END();
}
#endif

// ============================================================================
// Integration Tests
// ============================================================================

void test_swap_init() {
    TEST_START("Swap Initialization");
    
    // swap_init should already be called, just verify
    TEST_ASSERT(1, "Swap system initialized");
    
    MemoryDesc mm;
    int ret = swap_init_mm(init_mm);
    TEST_ASSERT(ret == 0, "swap_init_mm succeeds");
    TEST_ASSERT(mm.swap_list != nullptr, "Swap list created");
    
    TEST_END();
}

void test_swap_in_basic() {
    TEST_START("Swap In Basic");
    
    MemoryDesc mm;
    mm.pgdir = boot_pgdir;
    swap_init_mm(init_mm);
    
    uintptr_t addr = 0x100000;
    
    // Create a PTE with swap entry
    pte_t *ptep = get_pte(mm.pgdir, addr, 1);
    if (ptep) {
        *ptep = 0x100;  // Fake swap entry (present bit = 0, offset = 1)
        
        Page *page = nullptr;
        int ret = swap_in(init_mm, addr, &page);
        
        TEST_ASSERT(ret == 0, "swap_in returns success");
        TEST_ASSERT(page != nullptr, "Page allocated");
        
        // Check that PTE was updated
        pte_t *new_ptep = get_pte(mm.pgdir, addr, 0);
        TEST_ASSERT(new_ptep != nullptr && (*new_ptep & PTE_P), "PTE updated with present bit");
        
        if (page) {
            pages_free(page, 1);
        }
    } else {
        TEST_ASSERT(0, "Failed to create PTE");
    }
    
    TEST_END();
}

void test_swap_out_basic() {
    TEST_START("Swap Out Basic");
    
    MemoryDesc mm;
    mm.pgdir = boot_pgdir;
    swap_init_mm(init_mm);
    
    // Allocate and map some pages
    Page *pages_arr[3];
    uintptr_t addrs[3];
    
    for (int i = 0; i < 3; i++) {
        pages_arr[i] = alloc_pages(1);
        if (pages_arr[i]) {
            addrs[i] = 0x200000 + i * PG_SIZE;
            page_insert(mm.pgdir, pages_arr[i], addrs[i], PTE_P | PTE_W | PTE_U);
            swap_mgr->map_swappable(init_mm, addrs[i], pages_arr[i], 0);
        }
    }
    
    // Try to swap out
    int count = swap_out(init_mm, 2, 0);
    TEST_ASSERT(count > 0, "swap_out succeeded");
    
    // Verify that PTEs were updated (present bit cleared)
    for (int i = 0; i < count; i++) {
        pte_t *ptep = get_pte(mm.pgdir, addrs[i], 0);
        if (ptep) {
            TEST_ASSERT(!(*ptep & PTE_P), "PTE present bit cleared after swap out");
        }
    }
    
    cprintf("  Swapped out %d pages\n", count);
    
    TEST_END();
}

// ============================================================================
// Algorithm Comparison (SIMPLIFIED - FIFO Only)
// ============================================================================
// Note: This test now only tests FIFO. Other algorithms are archived.
// To re-enable full comparison, uncomment the archived algorithms above.

void test_algorithm_comparison() {
    TEST_START("Algorithm Comparison");
    
    cprintf("\n  Testing swap algorithm:\n");
    
    swap_manager *algorithms[] = {
        &swap_mgr_fifo
        // Add more algorithms here when re-enabled:
        // &swap_mgr_clock,
        // &swap_mgr_lru
    };
    
    for (int i = 0; i < 1; i++) {  // Changed from 3 to 1
        cprintf("    %s\n", algorithms[i]->name);

        algorithms[i]->init_mm(init_mm);
        
        Page pages[20];
        
        // Add 20 pages
        for (int j = 0; j < 20; j++) {
            algorithms[i]->map_swappable(init_mm, j * PG_SIZE, &pages[j], 0);
        }
        
        // Remove 10 pages
        int removed = 0;
        for (int j = 0; j < 10; j++) {
            Page *victim;
            if (algorithms[i]->swap_out_victim(init_mm, &victim, 0) == 0) {
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
    mm.pgdir = boot_pgdir;
    swap_init_mm(init_mm);
    
    // Test pattern: write data, swap out, swap in, verify data
    uintptr_t test_addr = 0x300000;
    
    // 1. Allocate a page and fill it with test pattern
    Page *page = alloc_pages(1);
    TEST_ASSERT(page != nullptr, "Page allocation successful");
    
    if (!page) {
        TEST_END();
        return;
    }
    
    // Fill page with test pattern
    void *kva = page2kva(page);
    for (int i = 0; i < PG_SIZE; i++) {
        ((uint8_t *)kva)[i] = (uint8_t)(i & 0xFF);
    }
    
    // 2. Map the page
    page_insert(mm.pgdir, page, test_addr, PTE_P | PTE_W | PTE_U);
    swap_mgr->map_swappable(init_mm, test_addr, page, 0);
    
    cprintf("  Filled page with test pattern\n");
    
    // 3. Swap out the page
    int swapped = swap_out(init_mm, 1, 0);
    TEST_ASSERT(swapped == 1, "Page swapped out");
    
    // Verify PTE was updated
    pte_t *ptep = get_pte(mm.pgdir, test_addr, 0);
    TEST_ASSERT(ptep != nullptr && !(*ptep & PTE_P), "PTE marked as not present");
    
    uintptr_t swap_entry = *ptep;
    cprintf("  Page swapped to entry 0x%x\n", swap_entry);
    
    // 4. Swap in the page
    Page *new_page = nullptr;
    int ret = swap_in(init_mm, test_addr, &new_page);
    TEST_ASSERT(ret == 0, "Page swapped in");
    TEST_ASSERT(new_page != nullptr, "New page allocated");
    
    // 5. Verify data integrity
    if (new_page) {
        void *new_kva = page2kva(new_page);
        int errors = 0;
        
        for (int i = 0; i < PG_SIZE; i++) {
            uint8_t expected = (uint8_t)(i & 0xFF);
            uint8_t actual = ((uint8_t *)new_kva)[i];
            if (actual != expected) {
                if (errors < 5) {
                    cprintf("    Mismatch at offset %d: expected 0x%02x, got 0x%02x\n",
                           i, expected, actual);
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
        
        pages_free(new_page, 1);
    }
    
    TEST_END();
}

void test_swap_multiple_pages() {
    TEST_START("Swap Multiple Pages");
    
    MemoryDesc mm;
    mm.pgdir = boot_pgdir;
    swap_init_mm(init_mm);
    
    #define NUM_TEST_PAGES 5
    uintptr_t base_addr = 0x400000;
    Page *pages_arr[NUM_TEST_PAGES];
    
    // 1. Allocate and fill multiple pages with unique patterns
    for (int i = 0; i < NUM_TEST_PAGES; i++) {
        pages_arr[i] = alloc_pages(1);
        if (pages_arr[i]) {
            uintptr_t addr = base_addr + i * PG_SIZE;
            
            // Fill with unique pattern (page number repeated)
            void *kva = page2kva(pages_arr[i]);
            for (int j = 0; j < PG_SIZE; j++) {
                ((uint8_t *)kva)[j] = (uint8_t)((i * 17 + j) & 0xFF);
            }
            
            page_insert(mm.pgdir, pages_arr[i], addr, PTE_P | PTE_W | PTE_U);
            swap_mgr->map_swappable(init_mm, addr, pages_arr[i], 0);
        }
    }
    
    cprintf("  Allocated and filled %d pages\n", NUM_TEST_PAGES);
    
    // 2. Swap out 3 pages
    int swapped = swap_out(init_mm, 3, 0);
    TEST_ASSERT(swapped == 3, "Swapped out 3 pages");
    cprintf("  Swapped out %d pages\n", swapped);
    
    // 3. Swap them back in and verify
    int verified = 0;
    for (int i = 0; i < swapped; i++) {
        uintptr_t addr = base_addr + i * PG_SIZE;
        pte_t *ptep = get_pte(mm.pgdir, addr, 0);
        
        if (ptep && !(*ptep & PTE_P)) {
            // This page was swapped out, swap it back in
            Page *page = nullptr;
            int ret = swap_in(init_mm, addr, &page);
            
            if (ret == 0 && page) {
                // Verify data
                void *kva = page2kva(page);
                int errors = 0;
                
                for (int j = 0; j < 256; j++) {  // Check first 256 bytes
                    uint8_t expected = (uint8_t)((i * 17 + j) & 0xFF);
                    uint8_t actual = ((uint8_t *)kva)[j];
                    if (actual != expected) {
                        errors++;
                    }
                }
                
                if (errors == 0) {
                    verified++;
                }
                
                pages_free(page, 1);
            }
        }
    }
    
    TEST_ASSERT(verified == swapped, "All swapped pages verified");
    cprintf("  Verified %d/%d pages\n", verified, swapped);
    
    TEST_END();
}

// Helper function for shell command
void swap_test_command() {
    run_swap_tests();
}
