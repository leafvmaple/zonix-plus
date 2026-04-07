#pragma once

#include "lib/result.h"
#include "pmm.h"
#include "vmm.h"

// Swap manager interface (abstract base class)
class SwapManager {
public:
    const char* name{};

    Error init();
    Error init_mm(MemoryDesc* mm);
    Error map_swappable(MemoryDesc* mm, uintptr_t addr, Page* page, int swap_in);
    Error swap_out_victim(MemoryDesc* mm, Page** page_ptr, int in_tick);
};

// Page-to-address mapping entry (for reverse lookup)
struct PageAddrMap {
    Page* page;
    uintptr_t addr;
    ListNode link;
};

using page_addr_map_t = PageAddrMap;

// Global functions
namespace swap {

inline constexpr size_t MAX_OFFSET_LIMIT = 1 << 24;  // 16 GB swap space limit

int init();
Error init_mm(MemoryDesc* mm);
Error in(MemoryDesc* mm, uintptr_t addr, Page** page_ptr);
int out(MemoryDesc* mm, int n, int in_tick);

// Swap disk operations
int swapfs_init();
Error swapfs_read(uintptr_t entry, Page* page);
Error swapfs_write(uintptr_t entry, Page* page);

// Helper function to find virtual address for a page
uintptr_t find_vaddr_for_page(MemoryDesc* mm, Page* page);

inline constexpr size_t MAX_OFFSET_LIMIT_COMPAT = MAX_OFFSET_LIMIT;

}  // namespace swap

inline constexpr size_t MAX_SWAP_OFFSET_LIMIT = swap::MAX_OFFSET_LIMIT;