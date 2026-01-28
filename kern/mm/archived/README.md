# Archived Swap Algorithms

This directory contains alternative swap page replacement algorithms that are currently not compiled into Zonix.

## Purpose

Zonix focuses on simplicity and concept learning. The FIFO algorithm is sufficient for basic understanding of swap mechanisms. These advanced algorithms are archived but preserved for future use.

## Archived Files

- **swap_lru.c / swap_lru.h**: Least Recently Used (LRU) page replacement algorithm
- **swap_clock.c / swap_clock.h**: Clock (Second Chance) page replacement algorithm

## Re-enabling These Algorithms

To re-enable these algorithms in the future:

1. Move the desired files back to `kern/mm/`
2. Add the corresponding `#include` statements in `swap_test.c`
3. Uncomment the test cases in `swap_test.c`
4. Update `swap.c` if you want to use a different default algorithm

## Algorithm Comparison

| Algorithm | Complexity | Performance | Best Use Case |
|-----------|-----------|-------------|---------------|
| FIFO      | O(1)      | Simple      | Learning, predictable workloads |
| Clock     | O(n)      | Good        | General purpose |
| LRU       | O(1)*     | Better      | Workloads with temporal locality |

*When implemented with proper data structures

## Notes

- Currently, only FIFO is compiled and used
- Test cases for LRU and Clock are disabled in `swap_test.c`
- These files are fully functional and tested
