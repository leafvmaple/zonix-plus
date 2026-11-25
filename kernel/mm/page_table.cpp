#include "page_table.h"
#include "drivers/intr.h"
#include "debug/assert.h"
#include "lib/memory.h"
#include "swap.h"
#include <asm/arch.h>
#include <asm/mmu.h>

namespace {

constexpr int PAGE_REF_INIT = 1;
constexpr int USER_PT_SUBTREE_DEPTH = PT_WALK_LEVELS - 2; /* depth passed to free_user_pt_subtree */
constexpr uintptr_t INVALID_TABLE_PA = static_cast<uintptr_t>(-1);

static uintptr_t alloc_table_page(bool create) {
    if (!create)
        return INVALID_TABLE_PA;

    Page* page = pmm::alloc_pages();
    if (page == nullptr)
        return INVALID_TABLE_PA;

    page->ref_count = PAGE_REF_INIT;
    page->set_page_table();
    uintptr_t pa = pmm::page_to_phys(page);
    memset(phys_to_virt(pa), 0, PG_SIZE);
    return pa;
}

static void free_table_page(uintptr_t pa) {
    Page* page = pmm::phys_to_page(pa);
    assert(page->ref_count == PAGE_REF_INIT);
    page->ref_count--;
    pmm::free_pages(page);
}

static inline void link_table_entry(pde_t* entry, uintptr_t pa) {
    *entry = make_pte_table(pa);
}

/*
 * Level-shift table for the page table walker.
 * LEVEL_SHIFTS[0] = root level shift  (PML4 / PGD)
 * LEVEL_SHIFTS[N-1] = leaf level shift (PT)
 * Used by both get_pte() and free_user_pt_subtree().
 */
static constexpr int LEVEL_SHIFTS[PAGE_LEVELS] = {PML4X_SHIFT, PDPTX_SHIFT, PDX_SHIFT, PTX_SHIFT};

static inline int level_index(uintptr_t va, int level) {
    return (va >> LEVEL_SHIFTS[level]) & 0x1FF;
}

/*
 * Descend one level in the page table.
 *
 * Given a pointer to a PDE at level N, return a pointer to the level-(N+1)
 * table it references.  Handles three cases:
 *
 *   1. Empty slot     → allocate a new page table (if create == true).
 *   2. Leaf (block)   → split into next-level entries, each covering
 *                        (1 << child_shift) bytes.
 *   3. Table pointer  → follow it.
 */
static pde_t* descend_level(pde_t* entry, bool create, int parent_shift, int child_shift) {
    if (pte_present(*entry)) {
        if (pte_is_block(*entry)) {
            uintptr_t large_pa = pte_addr(*entry) & ~((1UL << parent_shift) - 1);

            uintptr_t pa = alloc_table_page(create);
            if (pa == INVALID_TABLE_PA)
                return nullptr;

            auto* table = phys_to_virt<pde_t>(pa);
            for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
                table[i] = split_page_entry(*entry, large_pa + (static_cast<uintptr_t>(i) << child_shift), child_shift);
            }

            link_table_entry(entry, pa);
            return table;
        }
        return phys_to_virt<pde_t>(pte_addr(*entry));
    }

    uintptr_t pa = alloc_table_page(create);
    if (pa == INVALID_TABLE_PA)
        return nullptr;
    link_table_entry(entry, pa);
    return phys_to_virt<pde_t>(pa);
}

/*
 * Recursively free user-space page table subtree.
 *
 * @param table  Pointer to a page table at the given depth.
 * @param depth  Remaining depth below this table.
 *               depth == 0: entries are leaf PTEs (4KB pages).
 *               depth >  0: entries are either table pointers or large-page leaves.
 */
static void free_user_pt_subtree(pde_t* table, int depth) {
    for (int i = 0; i < ENTRY_NUM; i++) {
        pde_t entry = table[i];
        if (!pte_present(entry)) {
            if (depth == 0 && entry != 0) {
                swap::release_entry(entry);
            }
            continue;
        }

        if (depth == 0) {
            /* Leaf PTE — free the mapped physical page */
            Page* page = pmm::phys_to_page(pte_addr(entry));
            if (page->ref_count > 0)
                page->ref_count--;
            if (page->ref_count == 0) {
                page->list_node.unlink();
                pmm::free_pages(page);
            }
            continue;
        }

        if (pte_is_block(entry)) {
            /* Large-page leaf (2MB / 1GB) — skip in user teardown */
            continue;
        }

        pde_t* child = phys_to_virt<pde_t>(pte_addr(entry));
        free_user_pt_subtree(child, depth - 1);
        free_table_page(pte_addr(entry));
    }
}

// Tables below `table`, whose entries are leaves when depth is 0.
size_t count_tables(const pde_t* table, int depth) {
    if (depth == 0)
        return 0;
    size_t count = 0;
    for (int i = 0; i < ENTRY_NUM; i++) {
        const pde_t entry = table[i];
        if (pte_present(entry) && !pte_is_block(entry))
            count += 1 + count_tables(phys_to_virt<pde_t>(pte_addr(entry)), depth - 1);
    }
    return count;
}

}  // namespace

size_t pmm::table_page_count(const pde_t* pgdir) {
    size_t count = 0;
    for (int i = 0; i < ENTRY_NUM; i++) {
        const pde_t entry = pgdir[i];
        if (pte_present(entry) && !pte_is_block(entry))
            count += 1 + count_tables(phys_to_virt<pde_t>(pte_addr(entry)), PT_WALK_LEVELS - 2);
    }
    return count;
}

/*
 * Walk the multi-level page table and return a pointer to the leaf PTE
 * for virtual address @va.  If @create is true, intermediate tables and
 * large-page splits are allocated on-demand.
 *
 * Works for any PT_WALK_LEVELS (3 for Sv39, 4 for x86-64 / AArch64).
 */
pte_t* pmm::get_pte(pde_t* pgdir, uintptr_t va, bool create) {
    pde_t* table = pgdir;

    for (int level = 0; level < PT_WALK_LEVELS - 1; level++) {
        int idx = level_index(va, level);
        table = descend_level(table + idx, create, LEVEL_SHIFTS[level], LEVEL_SHIFTS[level + 1]);
        if (!table)
            return nullptr;
    }

    return table + level_index(va, PT_WALK_LEVELS - 1);
}

void pmm::unmap_mmio_page(pde_t* pgdir, uintptr_t va) {
    intr::Guard guard;
    pde_t* tables[PT_WALK_LEVELS]{pgdir};
    pde_t* entries[PT_WALK_LEVELS]{};
    int depth = 0;
    for (; depth < PT_WALK_LEVELS - 1; ++depth) {
        auto* entry = tables[depth] + level_index(va, depth);
        entries[depth] = entry;
        if (!pte_present(*entry) || pte_is_block(*entry)) {
            break;
        }
        tables[depth + 1] = phys_to_virt<pde_t>(pte_addr(*entry));
    }
    if (depth == PT_WALK_LEVELS - 1) {
        tables[depth][level_index(va, depth)] = 0;
        arch_invalidate_tlb_range(va, PG_SIZE);
    }
    // Also prune empty tables left by a failed create walk.
    for (; depth > 0; --depth) {
        bool empty = true;
        for (int i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
            if (tables[depth][i] != 0) {
                empty = false;
                break;
            }
        }
        uintptr_t pa = pte_addr(*entries[depth - 1]);
        if (!empty || (pa >> PG_SHIFT) >= pmm::page_count()) {
            break;
        }
        Page* page = phys_to_page(pa);
        if (!page->is_page_table() || page->is_reserved()) {
            break;
        }
        *entries[depth - 1] = 0;
        arch_invalidate_tlb_range(va, PG_SIZE);
        free_table_page(pa);
    }
}

Result<void*> pmm::user_address(pde_t* pgdir, uintptr_t user_va, bool write) {
    ENSURE(pgdir && user_va >= PG_SIZE && user_va < USER_SPACE_TOP);
    pde_t* table = pgdir;
    for (int level = 0; level < PT_WALK_LEVELS; ++level) {
        pde_t entry = table[level_index(user_va, level)];
        ENSURE(pte_present(entry));
        bool leaf = level == PT_WALK_LEVELS - 1 || pte_is_block(entry);
        if (leaf) {
            ENSURE(pte_user_accessible(entry, write));
            uintptr_t mask = (1ULL << LEVEL_SHIFTS[level]) - 1;
            return phys_to_virt((pte_addr(entry) & ~mask) + (user_va & mask));
        }
        ENSURE(pte_user_table_accessible(entry, write));
        table = phys_to_virt<pde_t>(pte_addr(entry));
    }
    return Error::Invalid;
}

void pmm::invalidate_tlb_page(pde_t* pgdir, uintptr_t va) {
    if (arch_read_page_table_root() == virt_to_phys(pgdir)) {
        arch_invalidate_tlb_page(reinterpret_cast<void*>(va));
    }
}

Page* pmm::alloc_and_map_page(pde_t* pgdir, uintptr_t va, uint32_t perm) {
    Page* page = pmm::alloc_pages(1);
    if (page) {
        if (pmm::page_insert(pgdir, page, va, perm) != Error::None) {
            pmm::free_pages(page);
            return nullptr;
        }
    }

    return page;
}

Error pmm::page_insert(pde_t* pgdir, Page* page, uintptr_t va, uint32_t perm) {
    pte_t* ptep = pmm::get_pte(pgdir, va, true);
    if (!ptep) {
        return Error::NoMem;
    }
    ENSURE(!pte_present(*ptep), Error::Exists);
    page->ref_count++;
    *ptep = make_pte_page(pmm::page_to_phys(page), perm);

    pmm::invalidate_tlb_page(pgdir, va);
    return Error::None;
}

void pmm::page_remove(pde_t* pgdir, uintptr_t va) {
    pte_t* ptep = pmm::get_pte(pgdir, va, false);
    if (!ptep || *ptep == 0) {
        return;
    }
    if (!pte_present(*ptep)) {
        swap::release_entry(*ptep);
    } else {
        Page* page = pmm::phys_to_page(pte_addr(*ptep));
        if (page->ref_count > 0)
            page->ref_count--;
        if (page->ref_count == 0) {
            page->list_node.unlink();
            pmm::free_pages(page);
        }
    }
    *ptep = 0;
    pmm::invalidate_tlb_page(pgdir, va);
}

void pmm::free_user_pgdir(pde_t* pgdir) {
    for (int i = 0; i < USER_TOP_ENTRIES; i++) {
        pde_t entry = pgdir[i];
        if (!pte_present(entry))
            continue;

        if (pte_is_block(entry))
            continue;

        pde_t* child = phys_to_virt<pde_t>(pte_addr(entry));
        free_user_pt_subtree(child, USER_PT_SUBTREE_DEPTH);
        free_table_page(pte_addr(entry));
    }

    kfree(pgdir);
}
