#pragma once

#include <base/types.h>
#include <asm/cpu.h>
#include <asm/page.h>

#include <debug/assert.h>
#include <sys/intrusive_list.hpp>
#include "lib/result.h"

// Page flags
enum class PageFlag : uint8_t {
    Reserved = 0,
    Property = 1,
    PageTable = 2,
    Heap = 3,
    Slab = 4,
};

// Page descriptor structures
struct Page {
    sys::first_member_hook list_node{};  // Must be the first member.
    int ref_count{};                     // Page frame reference count
    uint32_t flags{};                    // Bitmask of PageFlag
    unsigned int block_page_count{};     // Page span of a free run or kmalloc allocation

    void set_reserved() { flags |= (1 << static_cast<uint32_t>(PageFlag::Reserved)); }
    void set_page_table() { flags |= (1 << static_cast<uint32_t>(PageFlag::PageTable)); }
    bool is_page_table() const { return (flags & (1 << static_cast<uint32_t>(PageFlag::PageTable))) != 0; }

    void set_heap() { flags |= (1 << static_cast<uint32_t>(PageFlag::Heap)); }
    void set_slab() { flags |= (1 << static_cast<uint32_t>(PageFlag::Slab)); }
    bool is_heap() const { return (flags & (1 << static_cast<uint32_t>(PageFlag::Heap))) != 0; }
    bool is_slab() const { return (flags & (1 << static_cast<uint32_t>(PageFlag::Slab))) != 0; }

    void clear_reserved() { flags &= ~(1 << static_cast<uint32_t>(PageFlag::Reserved)); }

    [[nodiscard]] bool is_reserved() const { return (flags & (1 << static_cast<uint32_t>(PageFlag::Reserved))) != 0; }
};

static_assert(__is_standard_layout(Page));
static_assert(__builtin_offsetof(Page, list_node) == 0);

using PageList = sys::intrusive_list<Page, &Page::list_node>;

class FreeArea {
public:
    PageList free_list{};
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

private:
    inline static FreeArea free_area_{};
};

namespace pmm {

int init();

size_t page_count();
size_t free_page_count();
Page* alloc_pages(size_t page_count = 1);
void free_pages(Page* base, size_t page_count = 1);

void* page_to_kva(Page* page);
uintptr_t page_to_phys(Page* page);
Page* phys_to_page(uintptr_t pa);
Page* kva_to_page(void* kva);

}  // namespace pmm
