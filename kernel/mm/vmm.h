#pragma once

#include "lib/list.h"
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

    ListNode mmap_list{};  // VMA list ordered by start VA
    pde_t* pgdir{};        // Root page table for this address space
    int map_count{};       // Number of VMAs
    ListNode swap_list{};  // active swap queue for page replacement

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
Error copy_from_user(MemoryDesc* mm, void* kernel_dst, uintptr_t user_src_va, size_t byte_count);
Error copy_to_user(MemoryDesc* mm, uintptr_t user_dst_va, const void* kernel_src, size_t byte_count);
Error map_physical_range(pde_t* pgdir, uintptr_t va, size_t byte_count, uintptr_t pa, uint32_t perm);
uintptr_t mmio_map(uintptr_t pa, size_t byte_count, uint32_t perm);
void print_pgdir();

}  // namespace vmm
