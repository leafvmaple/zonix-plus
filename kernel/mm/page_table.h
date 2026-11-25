#pragma once

#include "mm/pmm.h"

namespace pmm {

// TLB and page table operations
void invalidate_tlb_page(pde_t* pgdir, uintptr_t va);

Page* alloc_and_map_page(pde_t* pgdir, uintptr_t va, uint32_t perm);
pte_t* get_pte(pde_t* pgdir, uintptr_t va, bool create);
// Remove an MMIO leaf without freeing its device physical address. Prune empty
// allocated tables, preserving reserved assembly-backed boot tables.
void unmap_mmio_page(pde_t* pgdir, uintptr_t va);
Error page_insert(pde_t* pgdir, Page* page, uintptr_t va, uint32_t perm);
// Remove a user leaf mapping, dropping its page reference or releasing its
// swap slot. Page tables stay until the address space is destroyed.
void page_remove(pde_t* pgdir, uintptr_t va);
// Validate every page-table level and return a kernel alias for a user address.
Result<void*> user_address(pde_t* pgdir, uintptr_t user_va, bool write);

// Free entire user address space (lower-half page tables + mapped pages + pgdir)
void free_user_pgdir(pde_t* pgdir);

// Counts the page-table pages below a root, not the root itself.
size_t table_page_count(const pde_t* pgdir);

}  // namespace pmm
