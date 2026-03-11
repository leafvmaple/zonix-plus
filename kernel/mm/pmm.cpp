#include "pmm.h"
#include "debug/assert.h"
#include "drivers/intr.h"

#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/math.h"

#include <asm/arch.h>
#include <asm/segments.h>
#include <asm/mmu.h>
#include <kernel/bootinfo.h>

// Declared in head.S — kernel's own copy of boot_info
extern struct boot_info __kernel_boot_info;

template<typename F>
void traverse_boot_mmap(F&& callback) {
    struct boot_info* bi = &__kernel_boot_info;
    // mmap_addr was set by the bootloader using physical addresses;
    // add KERNEL_BASE to access it from the higher-half kernel.
    auto* entries = reinterpret_cast<struct boot_mmap_entry*>(bi->mmap_addr + KERNEL_BASE);
    uint32_t count = bi->mmap_length;
    for (uint32_t i = 0; i < count; i++) {
        callback(entries[i].addr, entries[i].len, entries[i].type);
    }
}

#include "pmm_firstfit.h"

static union PMMStorage {
    alignas(16) char raw[sizeof(FirstFitPMMManager)];

    constexpr PMMStorage() noexcept : raw{} {}
    ~PMMStorage() {}
} _storge;

PMMManager* g_pmm{};

// Page management constants
namespace {

constexpr int PAGE_REF_INIT = 1;
constexpr size_t SINGLE_PAGE = 1;
constexpr int INSERT_FAILURE = 0;
constexpr int INSERT_SUCCESS = 1;

}  // namespace

uintptr_t boot_cr3;

long user_stack[PG_SIZE * 2];

long* STACK_START = &user_stack[PG_SIZE * 2];

Page* g_pages{};
uint32_t g_num_pages{};

inline size_t page_num(uintptr_t addr) {
    return addr >> PG_SHIFT;
}

uintptr_t pmm::page2pa(Page* page) {
    return static_cast<uintptr_t>((page - g_pages) << PG_SHIFT);
}

void* pmm::page2kva(Page* page) {
    return reinterpret_cast<void*>(KERNEL_BASE + pmm::page2pa(page));
}

Page* pmm::pa2page(uintptr_t pa) {
    return g_pages + page_num(pa);
}

// Convert kernel virtual address to page descriptor
Page* pmm::kva2page(void* kva) {
    return pmm::pa2page(virt_to_phys(kva));
}

static void pmm_mgr_init() {
    g_pmm = new (&_storge) FirstFitPMMManager();
    g_pmm->init();
    cprintf("pmm: manager = %s\n", g_pmm->get_name());
}

Page* pmm::alloc_pages(size_t n) {
    intr::Guard guard;
    Page* page = g_pmm->alloc(n);

    return page;
}

void pmm::free_pages(Page* base, size_t n) {
    intr::Guard guard;
    g_pmm->free(base, n);
}

pte_t* pmm::get_pte(pde_t* pml4, uintptr_t la, bool create) {
    // Walk 4-level page table: PML4 -> PDPT -> PD -> PT

    // Level 4: PML4
    pde_t* pml4e = pml4 + pml4x(la);
    if (!(*pml4e & PTE_P)) {
        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pa = page2pa(page);
        memset(phys_to_virt(pa), 0, PG_SIZE);
        *pml4e = pa | PTE_USER;
    }

    // Level 3: PDPT
    pde_t* pdpt = phys_to_virt<pde_t>(pte_addr(*pml4e));
    pde_t* pdpte = pdpt + pdptx(la);
    if (!(*pdpte & PTE_P)) {
        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pa = page2pa(page);
        memset(phys_to_virt(pa), 0, PG_SIZE);
        *pdpte = pa | PTE_USER;
    }

    // Level 2: PD
    pde_t* pd = phys_to_virt<pde_t>(pte_addr(*pdpte));
    pde_t* pde = pd + pdx(la);
    if (*pde & PTE_PS) {
        // This is a 2MB large page.  Split it into a 4KB page table so that
        // individual 4KB pages within the 2MB region can be managed.
        uintptr_t large_pa = pte_addr(*pde);         // base phys addr of the 2MB page
        uint64_t old_perm = *pde & 0xFFF & ~PTE_PS;  // keep original permissions minus PS

        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pt_pa = page2pa(page);
        pte_t* pt = phys_to_virt<pte_t>(pt_pa);

        // Fill the new PT: 512 entries covering the same 2MB range
        for (int i = 0; i < 512; i++) {
            pt[i] = (large_pa + i * PG_SIZE) | PTE_P | old_perm;
        }

        // Replace the 2MB PDE with a pointer to the new PT
        *pde = pt_pa | PTE_P | (old_perm & (PTE_W | PTE_U));
    } else if (!(*pde & PTE_P)) {
        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pa = page2pa(page);
        memset(phys_to_virt(pa), 0, PG_SIZE);
        *pde = pa | PTE_USER;
    }

    // Level 1: PT
    return phys_to_virt<pte_t>(pte_addr(*pde)) + ptx(la);
}

static void page_init() {
    uint64_t max_pa{};
    traverse_boot_mmap([&max_pa](uint64_t addr, uint64_t size, uint32_t type) {
        if (type == BOOT_MEM_AVAILABLE && max_pa < addr + size) {
            max_pa = addr + size;
        }
    });

    extern uint8_t KERNEL_END[];

    g_num_pages = page_num(max_pa);
    g_pages = reinterpret_cast<Page*>(round_up((void*)KERNEL_END, PG_SIZE));

    // Initially mark all g_pages as reserved
    for (uint32_t i = 0; i < g_num_pages; i++) {
        g_pages[i].set_reserved();
    }

    uintptr_t valid_mem = virt_to_phys(reinterpret_cast<uintptr_t>(g_pages + g_num_pages));
    traverse_boot_mmap([valid_mem](uint64_t addr, uint64_t size, uint32_t type) {
        if (type == BOOT_MEM_AVAILABLE) {
            uint64_t limit = addr + size;
            if (addr < valid_mem)
                addr = valid_mem;

            if (addr < limit) {
                addr = round_up(addr, PG_SIZE);
                limit = round_down(limit, PG_SIZE);

                cprintf("Valid Memory: [0x%016lx, 0x%016lx]\n", static_cast<uint64_t>(addr),
                        static_cast<uint64_t>(limit));
                g_pmm->init_memmap(pmm::pa2page(addr), page_num(limit - addr));
            }
        }
    });
}

void pmm::tlb_invl(pde_t* pgdir, uintptr_t la) {
    if (arch_read_cr3() == virt_to_phys(pgdir)) {
        arch_invlpg((void*)la);
    }
}

Page* pmm::pgdir_alloc_page(pde_t* pgdir, uintptr_t la, uint32_t perm) {
    Page* page = pmm::alloc_pages(SINGLE_PAGE);
    if (page) {
        pmm::page_insert(pgdir, page, la, perm);
    }

    return page;
}

int pmm::page_insert(pde_t* pgdir, Page* page, uintptr_t la, uint32_t perm) {
    pte_t* ptep = pmm::get_pte(pgdir, la, true);
    if (!ptep) {
        return INSERT_FAILURE;
    }
    page->ref++;
    *ptep = pmm::page2pa(page) | perm | PTE_P;

    pmm::tlb_invl(pgdir, la);
    return INSERT_SUCCESS;
}

void pmm::free_user_pgdir(pde_t* pgdir) {
    // Walk lower-half PML4 entries (0-255) — user space only
    for (int i = 0; i < USER_PML4_ENTRIES; i++) {
        if (!(pgdir[i] & PTE_P)) {
            continue;
        }

        pde_t* pdpt = phys_to_virt<pde_t>(pte_addr(pgdir[i]));
        for (int j = 0; j < ENTRY_NUM; j++) {
            if (!(pdpt[j] & PTE_P))
                continue;

            pde_t* pd = phys_to_virt<pde_t>(pte_addr(pdpt[j]));
            for (int k = 0; k < ENTRY_NUM; k++) {
                if (!(pd[k] & PTE_P))
                    continue;
                if (pd[k] & PTE_PS) {
                    // 2MB large page — skip (kernel identity map)
                    continue;
                }

                pte_t* pt = phys_to_virt<pte_t>(pte_addr(pd[k]));
                for (int l = 0; l < ENTRY_NUM; l++) {
                    if (pt[l] & PTE_P) {
                        Page* page = pa2page(pte_addr(pt[l]));
                        if (page->ref > 0)
                            page->ref--;
                        if (page->ref == 0)
                            free_page(page);
                    }
                }
                // Free the PT page itself
                free_page(pa2page(pte_addr(pd[k])));
            }
            // Free the PD page
            free_page(pa2page(pte_addr(pdpt[j])));
        }
        // Free the PDPT page
        free_page(pa2page(pte_addr(pgdir[i])));
    }
    // Free the PML4 page (pgdir itself, allocated via kmalloc)
    kfree(pgdir);
}

// ---------------------------------------------------------------------------
// kmalloc / kfree — general-purpose kernel heap (page-granularity)
//
// Allocates the minimum number of contiguous pages that can hold
// the requested size and records the page count in the first Page's
// `property` field (unused on allocated pages) so that kfree() can
// release the right amount without the caller tracking the size.
//
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
    return pmm::page2kva(page);
}

void kfree(void* ptr) {
    if (!ptr)
        return;

    Page* page = pmm::kva2page(ptr);
    size_t nr = page->property;
    if (nr == 0)
        nr = 1;  // defensive: property unset → assume 1 page

    pmm::free_pages(page, nr);
}

void pmm::init() {
    pmm_mgr_init();
    page_init();
}