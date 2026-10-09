#pragma once

#include "lib/list.h"
#include "lib/result.h"
#include "pmm.h"

// like Linux's mm_struct
struct MemoryDesc {
    enum class PageTableOwnership { Owned, Borrowed };

    explicit MemoryDesc(PageTableOwnership ownership = PageTableOwnership::Owned) : ownership_(ownership) {}

    ListNode mmap_list{};  // linear list link which sorted by start addr of vma
    pde_t* pgdir{};        // the PDT of these vma
    int map_count{};       // the count of these vma
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
    friend uintptr_t mmio_map(uintptr_t phys_addr, size_t size, uint32_t perm);
    // Boot assembly provides permanent storage; this MM must never free it.
    inline static MemoryDesc kernel_mm_{MemoryDesc::PageTableOwnership::Borrowed};
    static uintptr_t mmio_next_va_;
};

int init();
int pg_fault(MemoryDesc* mm, uint32_t error_code, uintptr_t addr);
bool user_range_valid(MemoryDesc* mm, uintptr_t addr, size_t size, bool write);
Error copy_from_user(MemoryDesc* mm, void* dst, uintptr_t src, size_t size);
Error copy_to_user(MemoryDesc* mm, uintptr_t dst, const void* src, size_t size);
Error pgdir_init(pde_t* pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm);
uintptr_t mmio_map(uintptr_t phys_addr, size_t size, uint32_t perm);
void print_pgdir();

}  // namespace vmm
