#include "pmm.h"
#include "debug/assert.h"
#include "drivers/intr.h"

#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/math.h"

#include <asm/arch.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <kernel/bootinfo.h>

// Declared in head.S — kernel's own copy of boot_info
extern struct BootInfo __kernel_boot_info;

template<typename F>
void traverse_boot_mmap(F&& callback) {
    struct BootInfo* bi = &__kernel_boot_info;
    auto* entries = reinterpret_cast<struct BootMemEntry*>(bi->mmap_addr + KERNEL_BASE);
    uint32_t count = bi->mmap_length;
    for (uint32_t i = 0; i < count; i++) {
        callback(entries[i].addr, entries[i].len, entries[i].type);
    }
}

// Page management constants and bootstrap helpers
namespace {

class Factory {
public:
    static int init() {
        allocator_.init();
        cprintf("pmm: allocator = %s\n", allocator_.name());
        return 0;
    }

    static PageAllocator& allocator() { return allocator_; }
    static Page* page_descriptors() { return page_desc_; }
    static uint32_t page_count() { return page_count_; }
    static void set_page_descriptors(Page* pages, uint32_t count) {
        page_desc_ = pages;
        page_count_ = count;
    }

private:
    inline static PageAllocator allocator_{};
    inline static Page* page_desc_{};
    inline static uint32_t page_count_{};
};

constexpr int PAGE_REF_INIT = 1;
constexpr int USER_PT_SUBTREE_DEPTH = PT_WALK_LEVELS - 2; /* depth passed to free_user_pt_subtree */
constexpr uintptr_t INVALID_TABLE_PA = static_cast<uintptr_t>(-1);

static bool normalize_available_range(uint64_t addr, uint64_t size, uintptr_t min_addr, uint64_t* out_begin,
                                      uint64_t* out_end) {
    if (size == 0)
        return false;

    uint64_t limit{};
    if (__builtin_add_overflow(addr, size, &limit)) {
        limit = static_cast<uint64_t>(-1);
    }

    uint64_t begin = (addr < min_addr) ? static_cast<uint64_t>(min_addr) : addr;
    if (begin >= limit)
        return false;

    begin = round_up(begin, PG_SIZE);
    limit = round_down(limit, PG_SIZE);
    if (begin >= limit)
        return false;

    *out_begin = begin;
    *out_end = limit;
    return true;
}

static uintptr_t alloc_table_page(bool create) {
    if (!create)
        return INVALID_TABLE_PA;

    Page* page = pmm::alloc_pages();
    if (page == nullptr)
        return INVALID_TABLE_PA;

    page->ref = PAGE_REF_INIT;
    uintptr_t pa = pmm::page_to_phys(page);
    memset(phys_to_virt(pa), 0, PG_SIZE);
    return pa;
}

static void free_table_page(uintptr_t pa) {
    Page* page = pmm::phys_to_page(pa);
    assert(page->ref == PAGE_REF_INIT);
    page->ref--;
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
static pde_t* descend_level(pde_t* entry, bool create, int child_shift) {
    if (pte_present(*entry)) {
        if (pte_is_block(*entry)) {
            uintptr_t large_pa = pte_addr(*entry);
            uint64_t old_perm = *entry & 0xFFF & ~VM_LARGEPAGE;

            uintptr_t pa = alloc_table_page(create);
            if (pa == INVALID_TABLE_PA)
                return nullptr;

            auto* table = phys_to_virt<pde_t>(pa);
            for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
                table[i] = make_pte_page(large_pa + (static_cast<uintptr_t>(i) << child_shift), old_perm);
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
        if (!pte_present(entry))
            continue;

        if (depth == 0) {
            /* Leaf PTE — free the mapped physical page */
            Page* page = pmm::phys_to_page(pte_addr(entry));
            if (page->ref > 0)
                page->ref--;
            if (page->ref == 0)
                pmm::free_pages(page);
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

}  // namespace

long user_stack[PG_SIZE * 2];
long* STACK_START = &user_stack[PG_SIZE * 2];

inline size_t page_num(uintptr_t addr) {
    return addr >> PG_SHIFT;
}

uintptr_t pmm::page_to_phys(Page* page) {
    return static_cast<uintptr_t>((page - Factory::page_descriptors()) << PG_SHIFT);
}

void* pmm::page_to_kva(Page* page) {
    return reinterpret_cast<void*>(KERNEL_BASE + pmm::page_to_phys(page));
}

Page* pmm::phys_to_page(uintptr_t pa) {
    return Factory::page_descriptors() + page_num(pa);
}

Page* pmm::kva_to_page(void* kva) {
    return pmm::phys_to_page(virt_to_phys(kva));
}

Page* pmm::alloc_pages(size_t n /*= 1*/) {
    intr::Guard guard;
    return Factory::allocator().alloc(n);
}

void pmm::free_pages(Page* base, size_t n /*= 1*/) {
    intr::Guard guard;
    Factory::allocator().free(base, n);
}

/*
 * Walk the multi-level page table and return a pointer to the leaf PTE
 * for virtual address @la.  If @create is true, intermediate tables and
 * large-page splits are allocated on-demand.
 *
 * Works for any PT_WALK_LEVELS (3 for Sv39, 4 for x86-64 / AArch64).
 */
pte_t* pmm::get_pte(pde_t* pgdir, uintptr_t la, bool create) {
    pde_t* table = pgdir;

    for (int level = 0; level < PT_WALK_LEVELS - 1; level++) {
        int idx = level_index(la, level);
        table = descend_level(table + idx, create, LEVEL_SHIFTS[level + 1]);
        if (!table)
            return nullptr;
    }

    return table + level_index(la, PT_WALK_LEVELS - 1);
}

Result<void*> pmm::user_address(pde_t* pgdir, uintptr_t addr, bool write) {
    ENSURE(pgdir && addr >= PG_SIZE && addr < USER_SPACE_TOP);
    pde_t* table = pgdir;
    for (int level = 0; level < PT_WALK_LEVELS; ++level) {
        pde_t entry = table[level_index(addr, level)];
        ENSURE(pte_present(entry));
        bool leaf = level == PT_WALK_LEVELS - 1 || pte_is_block(entry);
        if (leaf) {
            ENSURE(pte_user_accessible(entry, write));
            uintptr_t mask = (1ULL << LEVEL_SHIFTS[level]) - 1;
            return phys_to_virt((pte_addr(entry) & ~mask) + (addr & mask));
        }
        ENSURE(pte_user_table_accessible(entry, write));
        table = phys_to_virt<pde_t>(pte_addr(entry));
    }
    return Error::Invalid;
}

static int page_init() {
    BootInfo* bi = &__kernel_boot_info;

    if (bi->mmap_length == 0) {
        cprintf("pmm: boot memory map is empty\n");
        return -1;
    }

    uint64_t max_pa{};
    traverse_boot_mmap([&max_pa](uint64_t addr, uint64_t size, uint32_t type) {
        if (type == BOOT_MEM_AVAILABLE) {
            uint64_t limit{};
            if (__builtin_add_overflow(addr, size, &limit)) {
                limit = static_cast<uint64_t>(-1);
            }
            max_pa = (limit > max_pa) ? limit : max_pa;
        }
    });

    if (max_pa == 0) {
        cprintf("pmm: no usable memory regions in boot map (%d entries)\n", bi->mmap_length);
        return -1;
    }

    extern uint8_t KERNEL_END[];

    uint32_t page_count = page_num(round_up(max_pa, PG_SIZE));
    if (page_count == 0) {
        cprintf("pmm: max physical address too small (0x%lx)\n", static_cast<uint64_t>(max_pa));
        return -1;
    }

    Factory::set_page_descriptors(reinterpret_cast<Page*>(round_up(reinterpret_cast<void*>(KERNEL_END), PG_SIZE)),
                                  page_count);

    cprintf("pmm: %d pages, page array at [0x%p], max_pa=0x%lx\n", Factory::page_count(), Factory::page_descriptors(),
            static_cast<uint64_t>(max_pa));

    for (uint32_t i = 0; i < Factory::page_count(); i++) {
        Factory::page_descriptors()[i].set_reserved();
    }

    uintptr_t valid_mem = virt_to_phys(reinterpret_cast<uintptr_t>(Factory::page_descriptors() + Factory::page_count()));
    traverse_boot_mmap([valid_mem](uint64_t addr, uint64_t size, uint32_t type) {
        if (type != BOOT_MEM_AVAILABLE)
            return;

        uint64_t begin{};
        uint64_t limit{};
        if (!normalize_available_range(addr, size, valid_mem, &begin, &limit))
            return;

        cprintf("pmm: free region [0x%016lx, 0x%016lx]\n", static_cast<uint64_t>(begin), static_cast<uint64_t>(limit));
        Factory::allocator().init_memmap(pmm::phys_to_page(begin), page_num(limit - begin));
    });

    return 0;
}

void pmm::tlb_invl(pde_t* pgdir, uintptr_t la) {
    if (arch_read_page_table_root() == virt_to_phys(pgdir)) {
        arch_invlpg(reinterpret_cast<void*>(la));
    }
}

size_t pmm::free_page_count() {
    return Factory::allocator().free_page_count();
}

Page* pmm::pgdir_alloc_page(pde_t* pgdir, uintptr_t la, uint32_t perm) {
    Page* page = pmm::alloc_pages(1);
    if (page) {
        if (pmm::page_insert(pgdir, page, la, perm) != Error::None) {
            pmm::free_pages(page);
            return nullptr;
        }
    }

    return page;
}

Error pmm::page_insert(pde_t* pgdir, Page* page, uintptr_t la, uint32_t perm) {
    pte_t* ptep = pmm::get_pte(pgdir, la, true);
    if (!ptep) {
        return Error::NoMem;
    }
    page->ref++;
    *ptep = make_pte_page(pmm::page_to_phys(page), perm);

    pmm::tlb_invl(pgdir, la);
    return Error::None;
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

// TODO: Add a slab layer for sub-page objects to reduce waste.
// ---------------------------------------------------------------------------
void* kmalloc(size_t size) {
    if (size == 0)
        return nullptr;

    size_t nr = (size + PG_SIZE - 1) / PG_SIZE;  // pages needed
    Page* page = pmm::alloc_pages(nr);
    if (!page)
        return nullptr;

    page->property = nr;  // remember allocation size for kfree
    return pmm::page_to_kva(page);
}

void kfree(void* ptr) {
    if (!ptr)
        return;

    Page* page = pmm::kva_to_page(ptr);
    // defensive: property unset → assume 1 page
    pmm::free_pages(page, page->property > 0 ? page->property : 1);
}

int pmm::init() {
    Factory::init();

    int rc = page_init();
    if (rc != 0) {
        cprintf("pmm: page_init failed (rc=%d)\n", rc);
        return rc;
    }

    size_t free_pages = Factory::allocator().free_page_count();
    if (free_pages == 0) {
        cprintf("pmm: no free pages after initialization\n");
        return -1;
    }

    cprintf("pmm: initialized, %d free pages (%d MB)\n", static_cast<int>(free_pages),
            static_cast<int>((free_pages * PG_SIZE) / (1024ULL * 1024)));
    return 0;
}
