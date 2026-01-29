#pragma once

#include "list.h"
#include "pmm.h"

struct mm_struct {
    list_entry_t mmap_list{};         // linear list link which sorted by start addr of vma
    pde_t* pgdir{};                   // the PDT of these vma
    int map_count{};                  // the count of these vma
    list_entry_t* swap_list{};        // swap list for page replacement
};

int vmm_pg_fault(mm_struct* mm, uint32_t error_code, uintptr_t addr);

void vmm_init();
void print_pgdir();