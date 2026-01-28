#pragma once

#include <base/types.h>
#include <arch/x86/cpu.h>

#include "list.h"

#define PG_RESERVED 0
#define PG_PROPERTY 1 

typedef uintptr_t pte_t;   // Page Table Entry
typedef uintptr_t pde_t;   // Page Directory Entry

// Page descriptor structures
typedef struct {
    int ref;                   // page frame's reference counter
    uint32_t flags;
    unsigned int property;     // the num of free block, used in first fit pm manager
    list_entry_t page_link;    // free list link
} PageDesc;

typedef struct {
    const char *name;
    void (*init)();
    void (*init_memmap)(PageDesc *base, size_t n);
    PageDesc *(*alloc)(size_t n);
    void (*free)(PageDesc *base, size_t n);
    size_t (*nr_free_pages)();
    void (*check)();
} pmm_manager;

#define le2page(le, member) to_struct((le), PageDesc, member)

typedef struct {
    list_entry_t free_list;  // the list header
    unsigned int nr_free;    // # of free pages in this free list
} free_area_t;

#define SET_BIT(page, bit) ((page)->flags |= (1 << (bit)))
#define CLEAR_BIT(page, bit) ((page)->flags &= ~(1 << (bit)))
#define TEST_BIT(page, bit) ((page)->flags & (1 << (bit)))

#define SET_PAGE_RESERVED(page) (SET_BIT((page), PG_RESERVED))
#define CLEAR_PAGE_RESERVED(page) (CLEAR_BIT((page), PG_RESERVED))
#define PAGE_RESERVED(page) (TEST_BIT((page), PG_RESERVED))

#define alloc_page() alloc_pages(1)
#define free_page(page) pages_free((page), 1)

void tlb_invl(pde_t *pgdir, uintptr_t la);

void pmm_init();

PageDesc* pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);

pte_t* get_pte(pde_t *pgdir, uintptr_t la, int create);
int page_insert(pde_t *pgdir, PageDesc *page, uintptr_t la, uint32_t perm);

// Page allocation functions
PageDesc *alloc_pages(size_t n);
void pages_free(PageDesc* base, size_t n);

// Helper functions for page address conversion
void* page2kva(PageDesc *page);
uintptr_t page2pa(PageDesc *page);
PageDesc* pa2page(uintptr_t pa);
PageDesc* kva2page(void *kva);

// Memory allocation functions
void* kmalloc(size_t size);
void kfree(void* ptr);

