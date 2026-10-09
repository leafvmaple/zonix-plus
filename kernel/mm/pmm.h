#pragma once

#include <base/types.h>
#include <asm/cpu.h>
#include <asm/page.h>

#include "lib/list.h"
#include "lib/result.h"

// Page flags
enum class PageFlag : uint8_t {
    Reserved = 0,
    Property = 1,
};

// Page descriptor structures
struct Page {
    int ref_count{};                  // page frame's reference counter
    uint32_t flags{};                 // page flags (bitmask of PageFlag)
    unsigned int block_page_count{};  // Page span of a free run or kmalloc allocation
    ListNode list_node{};             // free list link

    void set_reserved() { flags |= (1 << static_cast<uint32_t>(PageFlag::Reserved)); }
    void clear_reserved() { flags &= ~(1 << static_cast<uint32_t>(PageFlag::Reserved)); }

    [[nodiscard]] bool is_reserved() const { return (flags & (1 << static_cast<uint32_t>(PageFlag::Reserved))) != 0; }

    [[nodiscard]] ListNode& node() { return list_node; }

    static constexpr size_t node_offset() { return offset_of(&Page::list_node); }
};

class FreeArea {
public:
    ListNode free_list{};
    unsigned int free_page_count{};
};

class PageAllocator {
public:
    [[nodiscard]] const char* name() const;

    void init();
    void init_memmap(Page* base, size_t page_count);

    Page* alloc(size_t page_count);
    void free(Page* base, size_t page_count);

    [[nodiscard]] size_t free_page_count() const;
};

namespace pmm {

int init();

// TLB and page table operations
void invalidate_tlb_page(pde_t* pgdir, uintptr_t va);

Page* alloc_and_map_page(pde_t* pgdir, uintptr_t va, uint32_t perm);
pte_t* get_pte(pde_t* pgdir, uintptr_t va, bool create);
Error page_insert(pde_t* pgdir, Page* page, uintptr_t va, uint32_t perm);
// Validate every page-table level and return a kernel alias for a user address.
Result<void*> user_address(pde_t* pgdir, uintptr_t user_va, bool write);

size_t free_page_count();
Page* alloc_pages(size_t page_count = 1);
void free_pages(Page* base, size_t page_count = 1);

void* page_to_kva(Page* page);
uintptr_t page_to_phys(Page* page);
Page* phys_to_page(uintptr_t pa);
Page* kva_to_page(void* kva);

// Free entire user address space (lower-half page tables + mapped pages + pgdir)
void free_user_pgdir(pde_t* pgdir);

}  // namespace pmm

// Kernel memory allocation (page-granularity, global library functions)
void* kmalloc(size_t byte_count);
void kfree(void* ptr);
