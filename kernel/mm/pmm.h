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

class PMMManager {
public:
    virtual void init() = 0;
    virtual void init_memmap(Page* base, size_t n) = 0;
    virtual Page* alloc(size_t n) = 0;
    virtual void free(Page* base, size_t n) = 0;
    virtual size_t nr_free_pages() = 0;
    virtual void check() = 0;

    PMMManager() = default;
    PMMManager(const PMMManager&) = delete;
    // Non-virtual destructor - PMM managers are never deleted via base pointer
    ~PMMManager() = default;
    PMMManager& operator=(const PMMManager&) = delete;

    [[nodiscard]] const char* get_name() const { return name_; }

protected:
    const char* name_{};
};

namespace pmm {

inline constexpr int SUCCESS = 0;
inline constexpr int FAILURE = -1;
inline constexpr Page* INVALID_PTR = nullptr;

void init();

// TLB and page table operations
void tlb_invl(pde_t* pgdir, uintptr_t la);
Page* pgdir_alloc_page(pde_t* pgdir, uintptr_t la, uint32_t perm);
pte_t* get_pte(pde_t* pml4, uintptr_t la, bool create);
int page_insert(pde_t* pgdir, Page* page, uintptr_t la, uint32_t perm);

// Page allocation
Page* alloc_pages(size_t n);
void free_pages(Page* base, size_t n);
inline Page* alloc_page() {
    return alloc_pages(1);
}
inline void free_page(Page* page) {
    free_pages(page, 1);
}

// Page address conversion helpers
void* page2kva(Page* page);
uintptr_t page2pa(Page* page);
Page* pa2page(uintptr_t pa);
Page* kva2page(void* kva);

}  // namespace pmm

// Kernel memory allocation (page-granularity, global library functions)
void* kmalloc(size_t size);
void kfree(void* ptr);

class FreeArea {
public:
    ListNode free_list{};
    unsigned int nr_free{};

    FreeArea() { free_list.init(); }
};
