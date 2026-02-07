#pragma once

#include "list.h"
#include "pmm.h"

// like Linux's mm_struct
struct MemoryDesc {
    ListNode mmap_list{};             // linear list link which sorted by start addr of vma
    pde_t* pgdir{};                   // the PDT of these vma
    int map_count{};                  // the count of these vma
    ListNode* swap_list{};            // swap list for page replacement
};

int vmm_pg_fault(MemoryDesc* mm, uint32_t error_code, uintptr_t addr);

void vmm_init();
void pgdir_init(pde_t* pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm);
void print_pgdir();