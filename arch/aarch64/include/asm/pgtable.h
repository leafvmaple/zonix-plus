#pragma once

#include <asm/page.h>

// Permanent kernel root page table supplied by this architecture's head.S.
extern "C" pde_t __kernel_pg_dir[PAGE_TABLE_ENTRIES];
static_assert(sizeof(__kernel_pg_dir) == PG_SIZE);
