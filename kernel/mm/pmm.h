#pragma once

#include <base/types.h>
#include <asm/cpu.h>

#include "lib/list.h"

using pte_t = uintptr_t;  // Page Table Entry
using pde_t = uintptr_t;  // Page Directory Entry

// Page flags
enum class PageFlag : uint8_t {
    Reserved = 0,
    Property = 1,
};

// Page descriptor structures
struct Page {
    int ref{};                // page frame's reference counter
    uint32_t flags{};         // page flags (bitmask of PageFlag)
    unsigned int property{};  // the num of free block, used in first fit pm manager
    ListNode list_node{};     // free list link

    void set_reserved() { flags |= (1 << static_cast<uint32_t>(PageFlag::Reserved)); }
    void clear_reserved() { flags &= ~(1 << static_cast<uint32_t>(PageFlag::Reserved)); }

    [[nodiscard]] bool is_reserved() const { return (flags & (1 << static_cast<uint32_t>(PageFlag::Reserved))) != 0; }

    [[nodiscard]] ListNode& node() { return list_node; }

    static constexpr size_t node_offset() { return offset_of(&Page::list_node); }
};

class FreeArea {
public:
    ListNode free_list{};
    unsigned int nr_free{};
};

class PageAllocator {
public:
    [[nodiscard]] const char* get_name() const;

    void init();
    void init_memmap(Page* base, size_t n);

    Page* alloc(size_t n);
    void free(Page* base, size_t n);

    [[nodiscard]] size_t free_page_count() const;
};

namespace pmm {

inline constexpr int SUCCESS = 0;
inline constexpr int FAILURE = -1;
inline constexpr Page* INVALID_PTR = nullptr;

int init();

// TLB and page table operations
void tlb_invl(pde_t* pgdir, uintptr_t la);

Page* pgdir_alloc_page(pde_t* pgdir, uintptr_t la, uint32_t perm);
pte_t* get_pte(pde_t* pml4, uintptr_t la, bool create);
int page_insert(pde_t* pgdir, Page* page, uintptr_t la, uint32_t perm);

Page* alloc_pages(size_t n = 1);
void free_pages(Page* base, size_t n = 1);

void* page_to_kva(Page* page);
uintptr_t page_to_phys(Page* page);
Page* phys_to_page(uintptr_t pa);
Page* kva_to_page(void* kva);

// Free entire user address space (lower-half page tables + mapped pages + pgdir)
void free_user_pgdir(pde_t* pgdir);

}  // namespace pmm

// Kernel memory allocation (page-granularity, global library functions)
void* kmalloc(size_t size);
void kfree(void* ptr);
