#include "pmm.h"
#include "../debug/assert.h"
#include "../arch/x86/e820.h"
#include "../drivers/intr.h"

#include "memory.h"
#include "stdio.h"
#include "math.h"

#include <asm/arch.h>
#include <asm/segments.h>
#include <asm/mmu.h>

#include "pmm_firstfit.h"

static union PMMStorage {
	alignas(16) char raw[sizeof(FirstFitPMMManager)];

	constexpr PMMStorage() noexcept : raw{} {}
    ~PMMStorage() {}
} _storge;

PMMManager* g_pmm{};

// Page management constants
namespace {

constexpr int PAGE_REF_INIT  = 1;
constexpr size_t SINGLE_PAGE = 1;
constexpr int INSERT_FAILURE = 0;
constexpr int INSERT_SUCCESS = 1;

} // namespace

uintptr_t boot_cr3;

long user_stack [ PG_SIZE * 2 ] ;

long* STACK_START = &user_stack [PG_SIZE * 2];

Page* g_pages{};
uint32_t g_num_pages{};

inline size_t page_num(uintptr_t addr) {
	return addr >> PG_SHIFT;
}

uintptr_t page2pa(Page* page) {
    return static_cast<uintptr_t>((page - g_pages) << PG_SHIFT);
}

void* page2kva(Page* page) {
    return reinterpret_cast<void*>(KERNEL_BASE + page2pa(page));
}

Page *pa2page(uintptr_t pa) {
	return g_pages + page_num(pa);
}

// Convert kernel virtual address to page descriptor
Page *kva2page(void *kva) {
    return pa2page(P_ADDR(reinterpret_cast<uintptr_t>(kva)));
}

static void pmm_mgr_init() {
	g_pmm = new (&_storge) FirstFitPMMManager();
    g_pmm->init();
	cprintf("pmm: manager = %s\n", g_pmm->m_name);
}

Page* alloc_pages(size_t n) {
	InterruptsGuard guard;
	Page *page = g_pmm->alloc(n);

	return page;
}

void pages_free(Page* base, size_t n) {
	InterruptsGuard guard;
	g_pmm->free(base, n);
}

pte_t* get_pte(pde_t* pml4, uintptr_t la, bool create) {
    // Walk 4-level page table: PML4 -> PDPT -> PD -> PT

    // Level 4: PML4
    pde_t* pml4e = pml4 + PML4X(la);
    if (!(*pml4e & PTE_P)) {
        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pa = page2pa(page);
        memset(K_ADDR(pa), 0, PG_SIZE);
        *pml4e = pa | PTE_USER;
    }

    // Level 3: PDPT
    pde_t* pdpt = reinterpret_cast<pde_t*>(K_ADDR(PTE_ADDR(*pml4e)));
    pde_t* pdpte = pdpt + PDPTX(la);
    if (!(*pdpte & PTE_P)) {
        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pa = page2pa(page);
        memset(K_ADDR(pa), 0, PG_SIZE);
        *pdpte = pa | PTE_USER;
    }

    // Level 2: PD
    pde_t* pd = reinterpret_cast<pde_t*>(K_ADDR(PTE_ADDR(*pdpte)));
    pde_t* pde = pd + PDX(la);
    if (*pde & PTE_PS) {
        // This is a 2MB large page.  Split it into a 4KB page table so that
        // individual 4KB pages within the 2MB region can be managed.
        uintptr_t large_pa = PTE_ADDR(*pde);    // base phys addr of the 2MB page
        uint64_t  old_perm = *pde & 0xFFF & ~PTE_PS;  // keep original permissions minus PS

        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr)
            return nullptr;
        page->ref = PAGE_REF_INIT;
        pde_t pt_pa = page2pa(page);
        pte_t* pt = reinterpret_cast<pte_t*>(K_ADDR(pt_pa));

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
        memset(K_ADDR(pa), 0, PG_SIZE);
        *pde = pa | PTE_USER;
    }

    // Level 1: PT
    return reinterpret_cast<pte_t*>(K_ADDR(PTE_ADDR(*pde))) + PTX(la);
}

static void page_init() {
	uint64_t max_pa{};
	traverse_e820_map([&max_pa](uint64_t addr, uint64_t size, uint32_t type) {
		if (type == E820_RAM && max_pa < addr + size) {
			max_pa = addr + size;
		}
	});

	extern uint8_t KERNEL_END[];

	g_num_pages = page_num(max_pa);
	g_pages = reinterpret_cast<Page *>(round_up((void*)KERNEL_END, PG_SIZE));
	
	// Initially mark all g_pages as reserved
	for (uint32_t i = 0; i < g_num_pages; i++) {
		g_pages[i].set_reserved();
	}
	
	uintptr_t valid_mem = P_ADDR(g_pages + g_num_pages);
	traverse_e820_map([valid_mem](uint64_t addr, uint64_t size, uint32_t type) {
		if (type == E820_RAM) {
			uint64_t limit = addr + size;
			if (addr < valid_mem)
				addr = valid_mem;

			if (addr < limit) {
				addr = round_up(addr, PG_SIZE);
				limit = round_down(limit, PG_SIZE);
				
    			cprintf("Valid Memory: [0x%016lx, 0x%016lx]\n", static_cast<uint64_t>(addr), static_cast<uint64_t>(limit));
				g_pmm->init_memmap(pa2page(addr), page_num(limit - addr));
			}
		}
	});
}

void tlb_invl(pde_t *pgdir, uintptr_t la) {
    if (arch_read_cr3() == P_ADDR(pgdir)) {
        arch_invlpg((void *)la);
    }
}

Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm) {
	Page *page = alloc_pages(SINGLE_PAGE);
	if (page) {
		page_insert(pgdir, page, la, perm);
	}

	return page;
}

int page_insert(pde_t *pgdir, Page *page, uintptr_t la, uint32_t perm) {
	pte_t *ptep = get_pte(pgdir, la, true);
	if (!ptep) {
		return INSERT_FAILURE;
	}
	page->ref++;
	*ptep = page2pa(page) | perm | PTE_P;

	tlb_invl(pgdir, la);
	return INSERT_SUCCESS;
}

// Simple kmalloc/kfree using page allocator
// For now, always allocate full g_pages (4KB)
// TODO: Implement proper slab allocator
void* kmalloc(size_t size) {
    Page* page = alloc_page();
    return page ? page2kva(page) : nullptr;
}

void kfree(void* ptr) {
    if (ptr) {
        free_page(kva2page(ptr));
    }
}

void pmm_init() {
	pmm_mgr_init();
	page_init();
}