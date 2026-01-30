#include "stdio.h"

#include "swap_fifo.h"
#include "swap.h"
#include "pmm.h"

#include <arch/x86/mmu.h>
#include "../drivers/blk.h"

// External function declarations
extern Page* alloc_pages(size_t n);
extern void pages_free(Page* base, size_t n);

// Global swap manager (can be changed to select different algorithms)
SwapManager* swap_mgr;

// Maximum swap offset (swap entries)
static unsigned int max_swap_offset;

// Swap device
static BlockDevice* swap_device = nullptr;

// Swap space configuration
namespace {

constexpr uint32_t SWAP_START_SECTOR = 1000;  // Start sector for swap space
constexpr size_t SECTORS_PER_PAGE = PG_SIZE / 512;  // Sectors needed for one page

} // namespace

int swap_init() {
    swap_mgr = &swap_mgr_fifo;  // Use FIFO swap manager for now
    swap_mgr->init();
    
    // Initialize swap filesystem (disk-based swap)
    swapfs_init();
    
    // Calculate maximum swap offset based on available disk space
    // Reserve space for swap (use sectors after SWAP_START_SECTOR)
    uint32_t available_sectors = swap_device->size - SWAP_START_SECTOR;
    max_swap_offset = available_sectors / SECTORS_PER_PAGE;
    
    cprintf("swap: manager = %s, available space = %d pages (%d MB)\n", 
            swap_mgr->name, max_swap_offset, (max_swap_offset * PG_SIZE) / (1024 * 1024));
    
    return 0;
}

/**
 * Initialize swap for a memory management struct
 */
int swap_init_mm(mm_struct *mm) {
    if (swap_mgr->init_mm) {
        return swap_mgr->init_mm(mm);
    }
    return 0;
}

/**
 * Swap in a page from disk to memory
 * @param mm: memory management struct
 * @param addr: virtual address that caused page fault
 * @param page_ptr: output pointer to the allocated page
 */
int swap_in(mm_struct *mm, uintptr_t addr, Page **page_ptr) {
    // Allocate a physical page
    Page *page = alloc_pages(1);
    if (page == nullptr) {
        cprintf("swap_in: failed to allocate page\n");
        return -1;
    }
    
    // Get the page table entry
    pte_t *ptep = get_pte(mm->pgdir, addr, 0);
    if (ptep == nullptr) {
        cprintf("swap_in: no page table entry\n");
        pages_free(page, 1);
        return -1;
    }
    
    // Read page from swap space (disk)
    // The swap entry is stored in the PTE
    uintptr_t swap_entry = *ptep;
    
    // Read from disk if swap device is available
    if (swapfs_read(swap_entry, page) != 0) {
        cprintf("swap_in: failed to read from swap\n");
        pages_free(page, 1);
        return -1;
    }
    
    cprintf("swap_in: loaded addr 0x%x from swap entry 0x%x to page %p\n",
            addr, swap_entry, page);
    
    // Update page table to map the virtual address to the new physical page
    page_insert(mm->pgdir, page, addr, PTE_P | PTE_W | PTE_U);
    
    // Mark page as swappable
    swap_mgr->map_swappable(mm, addr, page, 1);
    
    *page_ptr = page;
    return 0;
}

/**
 * Helper function to find virtual address for a page
 * Searches the page table to find the virtual address mapped to this page
 */
uintptr_t find_vaddr_for_page(mm_struct *mm, Page *page) {
    uintptr_t pa = page2pa(page);
    
    // Search through page directory
    for (int pde_idx = 0; pde_idx < 1024; pde_idx++) {
        pde_t pde = mm->pgdir[pde_idx];
        if (pde & PTE_P) {
            // Page table exists, search it
            pte_t *pt = (pte_t *)K_ADDR(PDE_ADDR(pde));
            for (int pte_idx = 0; pte_idx < 1024; pte_idx++) {
                pte_t pte = pt[pte_idx];
                if ((pte & PTE_P) && PTE_ADDR(pte) == pa) {
                    // Found it!
                    return (pde_idx << 22) | (pte_idx << 12);
                }
            }
        }
    }
    
    return 0;  // Not found
}

/**
 * Swap out pages from memory to disk
 * @param mm: memory management struct  
 * @param n: number of pages to swap out
 * @param in_tick: whether called from timer interrupt
 */
int swap_out(mm_struct *mm, int n, int in_tick) {
    int i;
    static uint32_t swap_offset = 1;  // Global swap offset counter
    
    for (i = 0; i < n; i++) {
        // Use swap manager to select a victim page
        Page *victim = nullptr;
        if (swap_mgr->swap_out_victim(mm, &victim, in_tick) != 0) {
            cprintf("swap_out: no victim page found\n");
            break;
        }
        
        if (victim == nullptr) {
            cprintf("swap_out: victim is nullptr\n");
            break;
        }
        
        // Find the virtual address that maps to this page
        uintptr_t victim_addr = find_vaddr_for_page(mm, victim);
        if (victim_addr == 0) {
            cprintf("swap_out: cannot find virtual address for page %p\n", victim);
            continue;
        }
        
        cprintf("swap_out: swapping out page %p at vaddr 0x%x\n", victim, victim_addr);
        
        // Get the page table entry
        pte_t *ptep = get_pte(mm->pgdir, victim_addr, 0);
        if (ptep == nullptr) {
            cprintf("swap_out: cannot get PTE for vaddr 0x%x\n", victim_addr);
            continue;
        }
        
        // Allocate a swap entry
        uintptr_t swap_entry = (swap_offset << 8);  // Create swap entry (present bit = 0)
        
        // Write page to swap space
        if (swapfs_write(swap_entry, victim) != 0) {
            cprintf("swap_out: failed to write to swap\n");
            continue;
        }
        
        // Update page table entry to indicate page is swapped out
        // Clear present bit and store swap entry
        *ptep = swap_entry;
        
        // Invalidate TLB for this address
        tlb_invl(mm->pgdir, victim_addr);
        
        // Free the physical page
        pages_free(victim, 1);
        
        // Increment swap offset for next allocation
        swap_offset++;
        if (swap_offset >= max_swap_offset) {
            swap_offset = 1;  // Wrap around (simple allocation)
        }
        
        cprintf("swap_out: successfully swapped out page to entry 0x%x\n", swap_entry);
    }
    
    return i;  // Return number of pages swapped out
}

/**
 * Initialize swap filesystem
 */
int swapfs_init(void) {
    // Get disk device
    swap_device = blk_get_device(BLK_TYPE_DISK);
    
    cprintf("swapfs init: using device '%s' for swap\n", swap_device->name);
    cprintf("swapfs init: swap starts at sector %d\n", SWAP_START_SECTOR);
    
    return 0;
}

/**
 * Read a page from swap space
 * @param entry: swap entry (page offset in swap space)
 * @param page: page descriptor to read into
 */
int swapfs_read(uintptr_t entry, Page *page) {
    // Calculate disk sector number
    // +--------------------------------+--------+---+
    // |    Swap Offset (24 bits)       | Reserved| P |
    // +--------------------------------+--------+---+
    // Bits 31-8                        Bits 7-1  Bit 0
    uint32_t offset = (entry >> 8) & 0xFFFFFF;  // Extract offset from swap entry
    uint32_t sector = SWAP_START_SECTOR + (offset * SECTORS_PER_PAGE);
    
    // Get kernel virtual address for the page
    void *kva = page2kva(page);
    
    // Read from disk
    if (blk_read(swap_device, sector, kva, SECTORS_PER_PAGE) != 0) {
        cprintf("swapfs_read: disk read failed (sector=%d)\n", sector);
        return -1;
    }
    
    cprintf("swapfs_read: read page from swap entry 0x%x (sector %d)\n", entry, sector);
    return 0;
}

/**
 * Write a page to swap space
 * @param entry: swap entry (page offset in swap space)
 * @param page: page descriptor to write from
 */
int swapfs_write(uintptr_t entry, Page *page) {
    // Calculate disk sector number
    uint32_t offset = (entry >> 8) & 0xFFFFFF;  // Extract offset from swap entry
    uint32_t sector = SWAP_START_SECTOR + (offset * SECTORS_PER_PAGE);
    
    // Get kernel virtual address for the page
    void *kva = page2kva(page);
    
    // Write to disk
    if (blk_write(swap_device, sector, kva, SECTORS_PER_PAGE) != 0) {
        cprintf("swapfs_write: disk write failed (sector=%d)\n", sector);
        return -1;
    }
    
    cprintf("swapfs_write: wrote page to swap entry 0x%x (sector %d)\n", entry, sector);
    return 0;
}