#pragma once

// Main test runner
void run_swap_tests();

// Shell command interface
void swap_test_command();

// Individual test functions (for selective testing)
void test_fifo_basic();
void test_fifo_interleaved();
void test_lru_basic();
void test_lru_access_pattern();
void test_clock_basic();
void test_swap_init();
void test_swap_in_basic();
void test_swap_out_basic();
void test_swap_disk_io();
void test_swap_multiple_pages();
void test_algorithm_comparison();
