#pragma once

#include "mm/page_table.h"

#include "lib/result.h"
#include "pmm.h"

// like Linux's mm_struct
struct MemoryDesc {
    enum class PageTableOwnership { Owned, Borrowed };

    explicit MemoryDesc(PageTableOwnership ownership = PageTableOwnership::Owned) : ownership_(ownership) {}
    MemoryDesc(const MemoryDesc&) = delete;
    MemoryDesc& operator=(const MemoryDesc&) = delete;
    MemoryDesc(MemoryDesc&&) = delete;
    MemoryDesc& operator=(MemoryDesc&&) = delete;

    pde_t* pgdir{};        // Root page table for this address space
    int map_count{};       // Number of VMAs
    PageList swap_list{};  // active swap queue for page replacement
    // The program break. The heap runs from heap_start, the page-aligned end
    // of the loaded image, to heap_end, and every page up to heap_end rounded
    // up is mapped. Both are 0 in an address space without a heap.
    uintptr_t heap_start{};
    uintptr_t heap_end{};

    ~MemoryDesc() {
        if (pgdir && ownership_ == PageTableOwnership::Owned) {
            pmm::free_user_pgdir(pgdir);
        }
    }

private:
    PageTableOwnership ownership_;
};

namespace vmm {

class Manager {
public:
    static pde_t* kernel_pgdir() { return kernel_mm_.pgdir; }
    static MemoryDesc& kernel_mm() { return kernel_mm_; }

private:
    friend int init();
    // Boot assembly provides permanent storage; this MM must never free it.
    inline static MemoryDesc kernel_mm_{MemoryDesc::PageTableOwnership::Borrowed};
};

int init();
int pg_fault(MemoryDesc* mm, uint32_t error_code, uintptr_t fault_va);
bool user_range_valid(MemoryDesc* mm, uintptr_t user_va, size_t byte_count, bool write);
// The highest program break: one unmapped guard page below the user stack.
inline constexpr uintptr_t HEAP_LIMIT = USER_STACK_TOP - USER_STACK_SIZE - PG_SIZE;
// Moves the program break to `end`, mapping zeroed pages to grow the heap and
// releasing them to shrink it, and returns the new break. An `end` of 0 only
// returns the current break. On failure the break and mappings are unchanged.
Result<uintptr_t> set_break(MemoryDesc* mm, uintptr_t end);
Error copy_from_user(MemoryDesc* mm, void* kernel_dst, uintptr_t user_src_va, size_t byte_count);
Error copy_to_user(MemoryDesc* mm, uintptr_t user_dst_va, const void* kernel_src, size_t byte_count);
Error map_physical_range(pde_t* pgdir, uintptr_t va, size_t byte_count, uintptr_t pa, uint32_t perm);
uintptr_t mmio_map(uintptr_t pa, size_t byte_count, uint32_t perm);
void print_pgdir();

}  // namespace vmm
