#pragma once

#include "lib/stdio.h"

// Shared test macros for all unit tests.
// Each test file should declare:
//   static int tests_passed = 0;
//   static int tests_failed = 0;

#define TEST_START(name)            \
    cprintf("\n[TEST] %s\n", name); \
    int __test_result = 1;

#define TEST_ASSERT(cond, msg)         \
    if (!(cond)) {                     \
        cprintf("  [FAIL] %s\n", msg); \
        __test_result = 0;             \
    } else {                           \
        cprintf("  [OK] %s\n", msg);   \
    }

#define TEST_END()               \
    if (__test_result) {         \
        cprintf("  [PASSED]\n"); \
        tests_passed++;          \
    } else {                     \
        cprintf("  [FAILED]\n"); \
        tests_failed++;          \
    }

#define TEST_SUMMARY(suite_name)                                             \
    cprintf("\n========================================\n");                  \
    cprintf("  %s - Summary\n", suite_name);                                 \
    cprintf("========================================\n");                    \
    cprintf("  Passed: %d\n", tests_passed);                                 \
    cprintf("  Failed: %d\n", tests_failed);                                 \
    cprintf("  Total:  %d\n", tests_passed + tests_failed);                  \
    if (tests_failed == 0) {                                                 \
        cprintf("\n  [SUCCESS] All %s tests passed!\n", suite_name);         \
    } else {                                                                 \
        cprintf("\n  [FAILURE] %d %s test(s) failed!\n", tests_failed,       \
                suite_name);                                                 \
    }                                                                        \
    cprintf("========================================\n");
