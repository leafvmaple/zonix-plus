#pragma once

#include "lib/result.h"
#include "pmm.h"
#include "vmm.h"

// FIFO page replacement policy
class SwapManager {
public:
    const char* name{};

    Error init();
    Error init_mm(MemoryDesc* mm);
    Error map_swappable(MemoryDesc* mm, uintptr_t va, Page* page, int from_swap_in);
    Error swap_out_victim(MemoryDesc* mm, Page** page_ptr, int in_timer_tick);
};

// Page-to-virtual-address mapping entry (for reverse lookup)
struct PageAddressMap {
    Page* page;
    uintptr_t va;
    ListNode link;
};


// Swap subsystem operations
namespace swap {

inline constexpr size_t MAX_OFFSET_LIMIT = 1 << 24;  // Exclusive bound of the 24-bit swap slot index
inline constexpr uintptr_t ENTRY_HAS_PERMISSIONS = 0x80;
inline constexpr uintptr_t ENTRY_WRITE = 0x02;

int init();
Error init_mm(MemoryDesc* mm);
Error in(MemoryDesc* mm, uintptr_t va, Page** page_ptr);
// Attempt up to page_count iterations; the return value includes iterations
// that continue after mapping or disk-write failures.
int out(MemoryDesc* mm, int page_count, int in_timer_tick);

// Swap disk operations
int swapfs_init();
Error swapfs_read(uintptr_t entry, Page* page);
Error swapfs_write(uintptr_t entry, Page* page);

// Helper function to find virtual address for a page
uintptr_t find_va_for_page(MemoryDesc* mm, Page* page);

inline constexpr size_t MAX_OFFSET_LIMIT_COMPAT = MAX_OFFSET_LIMIT;

}  // namespace swap

inline constexpr size_t MAX_SWAP_OFFSET_LIMIT = swap::MAX_OFFSET_LIMIT;
