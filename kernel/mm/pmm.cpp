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
extern struct boot_info __kernel_boot_info;

template<typename F>
void traverse_boot_mmap(F&& callback) {
    struct boot_info* bi = &__kernel_boot_info;
    auto* entries = reinterpret_cast<struct boot_mmap_entry*>(bi->mmap_addr + KERNEL_BASE);
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
        s_allocator.init();
        cprintf("pmm: allocator = %s\n", s_allocator.get_name());
        return 0;
    }

    inline static PageAllocator s_allocator{};
    inline static Page* s_page_desc{};
    inline static uint32_t s_page_count{};
};

constexpr int PAGE_REF_INIT = 1;
constexpr int USER_PT_LEVEL_PDPT = 2;
constexpr int USER_PT_LEVEL_PD = 1;
constexpr int USER_PT_LEVEL_PT = 0;
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

static inline void link_table_entry(pde_t* entry, uintptr_t pa) {
    *entry = make_pte_table(pa);
}

static pde_t* ensure_table_entry(pde_t* entry, bool create) {
    if (!(*entry & VM_PRESENT)) {
        uintptr_t pa = alloc_table_page(create);
        if (pa == INVALID_TABLE_PA)
            return nullptr;
        link_table_entry(entry, pa);
    }

    return phys_to_virt<pde_t>(pte_addr(*entry));
}

static pte_t* ensure_pt_from_pd_entry(pde_t* entry, bool create) {
    if (pte_is_block(*entry)) {  // 2MB Large Page
        uintptr_t large_pa = pte_addr(*entry);
        uint64_t old_perm = *entry & 0xFFF & ~VM_LARGEPAGE;

        uintptr_t pa = alloc_table_page(create);
        if (pa == INVALID_TABLE_PA)
            return nullptr;

        auto* pt = phys_to_virt<pte_t>(pa);
        for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt[i] = make_pte_page(large_pa + static_cast<uintptr_t>(i) * PG_SIZE, old_perm);
        }

        link_table_entry(entry, pa);

        return pt;
    }

    if (!(*entry & VM_PRESENT)) {
        uintptr_t pa = alloc_table_page(create);
        if (pa == INVALID_TABLE_PA)
            return nullptr;
        link_table_entry(entry, pa);
    }

    return phys_to_virt<pte_t>(pte_addr(*entry));
}

static void free_user_pt_subtree(pde_t* table, int level) {
    for (int i = 0; i < ENTRY_NUM; i++) {
        pde_t entry = table[i];
        if (!(entry & VM_PRESENT))
            continue;

        if (level == USER_PT_LEVEL_PT) {
            Page* page = pmm::phys_to_page(pte_addr(entry));
            if (page->ref > 0)
                page->ref--;
            if (page->ref == 0)
                pmm::free_pages(page);
            continue;
        }

        if (level == USER_PT_LEVEL_PD && (entry & VM_LARGEPAGE)) {
            // Keep behavior: skip large mappings in user teardown path.
            continue;
        }

        pde_t* child = phys_to_virt<pde_t>(pte_addr(entry));
        free_user_pt_subtree(child, level - 1);
        pmm::free_pages(pmm::phys_to_page(pte_addr(entry)));
    }
}

}  // namespace

long user_stack[PG_SIZE * 2];
long* STACK_START = &user_stack[PG_SIZE * 2];

inline size_t page_num(uintptr_t addr) {
    return addr >> PG_SHIFT;
}

uintptr_t pmm::page_to_phys(Page* page) {
    return static_cast<uintptr_t>((page - Factory::s_page_desc) << PG_SHIFT);
}

void* pmm::page_to_kva(Page* page) {
    return reinterpret_cast<void*>(KERNEL_BASE + pmm::page_to_phys(page));
}

Page* pmm::phys_to_page(uintptr_t pa) {
    return Factory::s_page_desc + page_num(pa);
}

Page* pmm::kva_to_page(void* kva) {
    return pmm::phys_to_page(virt_to_phys(kva));
}

Page* pmm::alloc_pages(size_t n /*= 1*/) {
    intr::Guard guard;
    return Factory::s_allocator.alloc(n);
}

void pmm::free_pages(Page* base, size_t n /*= 1*/) {
    intr::Guard guard;
    Factory::s_allocator.free(base, n);
}

pte_t* pmm::get_pte(pde_t* pml4, uintptr_t la, bool create) {
    pde_t* pdpt = ensure_table_entry(pml4 + pml4_index(la), create);
    if (!pdpt)
        return nullptr;

    pde_t* pd = ensure_table_entry(pdpt + pdpt_index(la), create);
    if (!pd)
        return nullptr;

    pte_t* pt = ensure_pt_from_pd_entry(pd + pd_index(la), create);
    return pt ? pt + pt_index(la) : nullptr;
}

static int page_init() {
    boot_info* bi = &__kernel_boot_info;

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

    Factory::s_page_count = page_num(round_up(max_pa, PG_SIZE));
    if (Factory::s_page_count == 0) {
        cprintf("pmm: max physical address too small (0x%lx)\n", static_cast<uint64_t>(max_pa));
        return -1;
    }

    Factory::s_page_desc = reinterpret_cast<Page*>(round_up((void*)KERNEL_END, PG_SIZE));

    cprintf("pmm: %d pages, page array at [0x%p], max_pa=0x%lx\n", Factory::s_page_count, Factory::s_page_desc,
            static_cast<uint64_t>(max_pa));

    for (uint32_t i = 0; i < Factory::s_page_count; i++) {
        Factory::s_page_desc[i].set_reserved();
    }

    uintptr_t valid_mem = virt_to_phys(reinterpret_cast<uintptr_t>(Factory::s_page_desc + Factory::s_page_count));
    traverse_boot_mmap([valid_mem](uint64_t addr, uint64_t size, uint32_t type) {
        if (type != BOOT_MEM_AVAILABLE)
            return;

        uint64_t begin{};
        uint64_t limit{};
        if (!normalize_available_range(addr, size, valid_mem, &begin, &limit))
            return;

        cprintf("pmm: free region [0x%016lx, 0x%016lx]\n", static_cast<uint64_t>(begin), static_cast<uint64_t>(limit));
        Factory::s_allocator.init_memmap(pmm::phys_to_page(begin), page_num(limit - begin));
    });

    return 0;
}

void pmm::tlb_invl(pde_t* pgdir, uintptr_t la) {
    if (arch_read_cr3() == virt_to_phys(pgdir)) {
        arch_invlpg((void*)la);
    }
}

Page* pmm::pgdir_alloc_page(pde_t* pgdir, uintptr_t la, uint32_t perm) {
    Page* page = pmm::alloc_pages(1);
    if (page) {
        pmm::page_insert(pgdir, page, la, perm);
    }

    return page;
}

int pmm::page_insert(pde_t* pgdir, Page* page, uintptr_t la, uint32_t perm) {
    pte_t* ptep = pmm::get_pte(pgdir, la, true);
    if (!ptep) {
        return -1;
    }
    page->ref++;
    *ptep = make_pte_page(pmm::page_to_phys(page), perm);

    pmm::tlb_invl(pgdir, la);
    return 0;
}

void pmm::free_user_pgdir(pde_t* pgdir) {
    for (int i = 0; i < USER_TOP_ENTRIES; i++) {
        pde_t entry = pgdir[i];
        if (!(entry & VM_PRESENT))
            continue;

        pde_t* pdpt = phys_to_virt<pde_t>(pte_addr(entry));
        free_user_pt_subtree(pdpt, USER_PT_LEVEL_PDPT);
        pmm::free_pages(phys_to_page(pte_addr(entry)));
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

    size_t free_pages = Factory::s_allocator.free_page_count();
    if (free_pages == 0) {
        cprintf("pmm: no free pages after initialization\n");
        return FAILURE;
    }

    cprintf("pmm: initialized, %d free pages (%d MB)\n", static_cast<int>(free_pages),
            static_cast<int>((free_pages * PG_SIZE) / (1024ULL * 1024)));
    return SUCCESS;
}