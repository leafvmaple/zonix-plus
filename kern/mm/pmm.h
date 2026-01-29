#pragma once

#include <base/types.h>
#include <arch/x86/cpu.h>

#include "list.h"

using pte_t = uintptr_t;   // Page Table Entry
using pde_t = uintptr_t;   // Page Directory Entry

// Page descriptor structures
struct Page {
    enum {
        PG_RESERVED,
        PG_PROPERTY,
    };

    int ref{};                   // page frame's reference counter
    uint32_t flags{};
    unsigned int property{};     // the num of free block, used in first fit pm manager
    list_entry_t page_link{};    // free list link

    inline void set_reserved() {
        flags |= (1 << PG_RESERVED);
    }

    inline void clear_reserved() {
        flags &= ~(1 << PG_RESERVED);
    }

    inline bool is_reserved() const {
        return (flags & (1 << PG_RESERVED)) != 0;
    }

    static constexpr list_entry_t* link_offset() {
        return &((Page *)0)->page_link;
    }
};

class PMMManager {
public:
    const char* m_name{};

    virtual void init() = 0;
    virtual void init_memmap(Page* base, size_t n) = 0;
    virtual Page* alloc(size_t n) = 0;
    virtual void free(Page* base, size_t n) = 0;
    virtual size_t nr_free_pages() = 0;
    virtual void check() = 0;
    
    PMMManager() = default;
};

#define le2page(le, member) to_struct((le), Page, member)

class FreeArea {
public:
    list_entry_t free_list{};
    unsigned int nr_free{};

    FreeArea() {
        list_init(&free_list);
    }
};

void tlb_invl(pde_t* pgdir, uintptr_t la);

void pmm_init();

Page* pgdir_alloc_page(pde_t* pgdir, uintptr_t la, uint32_t perm);

pte_t* get_pte(pde_t* pgdir, uintptr_t la, int create);
int page_insert(pde_t* pgdir, Page* page, uintptr_t la, uint32_t perm);

// Page allocation functions
Page* alloc_pages(size_t n);
void pages_free(Page* base, size_t n);
inline Page* alloc_page() {
    return alloc_pages(1);
}
inline void free_page(Page* page) {
    pages_free(page, 1);
}

// Helper functions for page address conversion
void* page2kva(Page* page);
uintptr_t page2pa(Page* page);
Page* pa2page(uintptr_t pa);
Page* kva2page(void* kva);

// Memory allocation functions
void* kmalloc(size_t size);
void kfree(void* ptr);

