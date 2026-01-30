#include "pmm.h"
#include "../debug/assert.h"
#include "../arch/x86/e820.h"
#include "../drivers/intr.h"

#include "memory.h"
#include "stdio.h"
#include "math.h"

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>
#include <arch/x86/segments.h>
#include <arch/x86/mmu.h>

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

long user_stack [ PG_SIZE >> 2 ] ;

long* STACK_START = &user_stack [PG_SIZE >> 2];

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
    return pa2page(P_ADDR((uintptr_t)kva));
}

static void pmm_mgr_init() {
	g_pmm = new (&_storge) FirstFitPMMManager();
    g_pmm->init();
	cprintf("pmm: manager = %s\n", g_pmm->m_name);
}

Page* alloc_pages(size_t n) {
	intr_save();
	Page *page = g_pmm->alloc(n);
	intr_restore();

	return page;
}

void pages_free(Page* base, size_t n) {
	intr_save();
	g_pmm->free(base, n);
	intr_restore();
}

pte_t* get_pte(pde_t* pgdir, uintptr_t la, bool create) {
    pde_t* pdep = pgdir + PDX(la);
    if (!(*pdep & PTE_P)) {
        Page* page{};
        if (!create || (page = alloc_pages(1)) == nullptr) {
            return nullptr;
        }
        page->ref = PAGE_REF_INIT;

        pde_t pa = page2pa(page);
        memset(K_ADDR(pa), 0, PG_SIZE);
        *pdep = pa | PTE_USER;
    }
    return (pte_t*)K_ADDR(PDE_ADDR(*pdep)) + PTX(la);
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
	g_pages = (Page *)round_up((void*)KERNEL_END, PG_SIZE);
	
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
				
    			cprintf("Valid Memory: [0x%08x, 0x%08x]\n", (uint32_t)addr, (uint32_t)limit);
				g_pmm->init_memmap(pa2page(addr), page_num(limit - addr));
			}
		}
	});
}

void tlb_invl(pde_t *pgdir, uintptr_t la) {
    if (rcr3() == P_ADDR(pgdir)) {
        invlpg((void *)la);
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