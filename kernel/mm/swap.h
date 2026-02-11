#pragma once

#include "pmm.h"
#include "vmm.h"

// Swap manager interface (abstract base class)
class SwapManager {
public:
    const char* name{};

    virtual ~SwapManager() = default;
    virtual int init() = 0;
    virtual int init_mm(MemoryDesc* mm) = 0;
    virtual int map_swappable(MemoryDesc* mm, uintptr_t addr, Page* page, int swap_in) = 0;
    virtual int swap_out_victim(MemoryDesc* mm, Page** page_ptr, int in_tick) = 0;
    virtual int check_swap() = 0;

    SwapManager() = default;
    SwapManager(const SwapManager&) = delete;
    SwapManager& operator=(const SwapManager&) = delete;
};

// Page-to-address mapping entry (for reverse lookup)
struct PageAddrMap {
    Page* page;
    uintptr_t addr;
    ListNode link;
};

using page_addr_map_t = PageAddrMap;

// Global functions
int swap_init();
int swap_init_mm(MemoryDesc* mm);
int swap_in(MemoryDesc* mm, uintptr_t addr, Page** page_ptr);
int swap_out(MemoryDesc* mm, int n, int in_tick);

// Swap disk operations (to be implemented with disk driver)
int swapfs_init();
int swapfs_read(uintptr_t entry, Page* page);
int swapfs_write(uintptr_t entry, Page* page);

// Helper function to find virtual address for a page
uintptr_t find_vaddr_for_page(MemoryDesc* mm, Page* page);

namespace swap {

inline constexpr size_t MAX_OFFSET_LIMIT = 1 << 24;  // 16 GB swap space limit

} // namespace swap

inline constexpr size_t MAX_SWAP_OFFSET_LIMIT = swap::MAX_OFFSET_LIMIT;