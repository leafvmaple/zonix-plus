#include "lib/stdio.h"

#include "swap.h"
#include "pmm.h"

#include <asm/page.h>
#include <asm/mmu.h>
#include "block/blk.h"

namespace {

constexpr uint32_t SWAP_START_SECTOR = 1000;        // Start sector for swap space
constexpr size_t SECTORS_PER_PAGE = PG_SIZE / 512;  // Sectors needed for one page

class State {
    friend int swap::init();
    friend Error swap::init_mm(MemoryDesc* mm);
    friend Error swap::in(MemoryDesc* mm, uintptr_t addr, Page** page_ptr);
    friend int swap::out(MemoryDesc* mm, int n, int in_tick);
    friend int swap::swapfs_init();
    friend Error swap::swapfs_read(uintptr_t entry, Page* page);
    friend Error swap::swapfs_write(uintptr_t entry, Page* page);

    inline static SwapManager manager_{};
    inline static unsigned int max_offset_{};
    inline static BlockDevice* device_{};
    inline static uint32_t next_offset_ = 1;
};

}  // namespace

namespace swap {

int init() {
    Error rc = State::manager_.init();
    if (rc != Error::None) {
        cprintf("swap: swap manager '%s' init failed (%s)\n", State::manager_.name, error_str(rc));
        return -1;
    }

    if (swapfs_init() != 0) {
        cprintf("swap: no swap device available (non-fatal)\n");
    }

    if (!State::device_) {
        State::max_offset_ = 0;
        cprintf("swap: disabled (no swap device)\n");
        return 0;
    }

    if (State::device_->block_count <= SWAP_START_SECTOR) {
        cprintf("swap: device '%s' too small (%d sectors, need > %d)\n", State::device_->name,
                State::device_->block_count, SWAP_START_SECTOR);
        State::max_offset_ = 0;
        return 0;
    }

    uint32_t available_sectors = State::device_->block_count - SWAP_START_SECTOR;
    State::max_offset_ = available_sectors / SECTORS_PER_PAGE;

    if (State::max_offset_ == 0) {
        cprintf("swap: device '%s' has no usable swap space\n", State::device_->name);
        return 0;
    }

    cprintf("swap: manager=%s, device='%s', %d pages (%d MB)\n", State::manager_.name, State::device_->name,
            State::max_offset_, (State::max_offset_ * PG_SIZE) / (1024 * 1024));

    return 0;
}

Error init_mm(MemoryDesc* mm) {
    return State::manager_.init_mm(mm);
}

Error in(MemoryDesc* mm, uintptr_t addr, Page** page_ptr) {
    ENSURE(mm && mm->pgdir && page_ptr);
    *page_ptr = nullptr;
    Page* page = pmm::alloc_pages(1);
    if (page == nullptr) {
        cprintf("swap_in: failed to allocate page\n");
        return Error::NoMem;
    }

    pte_t* ptep = pmm::get_pte(mm->pgdir, addr, 0);
    if (ptep == nullptr) {
        cprintf("swap_in: no page table entry\n");
        pmm::free_pages(page, 1);
        return Error::NotFound;
    }

    uintptr_t swap_entry = *ptep;
    if (swap_entry == 0 || pte_present(swap_entry)) {
        pmm::free_pages(page);
        return Error::Invalid;
    }
    if (swapfs_read(swap_entry, page) != Error::None) {
        cprintf("swap_in: failed to read from swap\n");
        pmm::free_pages(page, 1);
        return Error::Io;
    }

    cprintf("swap_in: loaded addr 0x%x from swap entry 0x%x to page %p\n", addr, swap_entry, page);

    // Bit 7 marks entries that preserve the original write permission.
    uint32_t perm = user_page_perm((swap_entry & ENTRY_HAS_PERMISSIONS) == 0 || (swap_entry & ENTRY_WRITE) != 0);
    if (pmm::page_insert(mm->pgdir, page, addr, perm) != Error::None) {
        pmm::free_pages(page);
        return Error::NoMem;
    }

    State::manager_.map_swappable(mm, addr, page, 1);

    *page_ptr = page;
    return Error::None;
}

}  // namespace swap

namespace {

constexpr int LEVEL_SHIFTS[PAGE_LEVELS] = {PML4X_SHIFT, PDPTX_SHIFT, PDX_SHIFT, PTX_SHIFT};

// Recursively walk the page table tree looking for a mapping to target_pa.
// depth: 0 = PML4, 1 = PDPT, 2 = PD, 3 = PT (leaf)
uintptr_t scan_pt_for_pa(const pde_t* table, int depth, uintptr_t va_base, uintptr_t target_pa) {
    int shift = LEVEL_SHIFTS[depth];
    bool is_leaf = (depth == PAGE_LEVELS - 1);

    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        pde_t entry = table[i];
        if (!pte_present(entry))
            continue;

        uintptr_t va = va_base | (static_cast<uintptr_t>(i) << shift);

        if (is_leaf) {
            if (pte_addr(entry) == target_pa)
                return va;
            continue;
        }

        if (pte_is_block(entry)) {
            uintptr_t block_pa = pte_addr(entry);
            uintptr_t block_size = 1UL << shift;
            if (target_pa >= block_pa && target_pa < block_pa + block_size)
                return va | (target_pa - block_pa);
            continue;
        }

        uintptr_t result = scan_pt_for_pa(phys_to_virt<pde_t>(pte_addr(entry)), depth + 1, va, target_pa);
        if (result != 0)
            return result;
    }

    return 0;
}

}  // namespace

uintptr_t swap::find_vaddr_for_page(MemoryDesc* mm, Page* page) {
    return scan_pt_for_pa(mm->pgdir, 0, 0, pmm::page_to_phys(page));
}

namespace swap {

int out(MemoryDesc* mm, int n, int in_tick) {
    int i{};

    for (i = 0; i < n; i++) {
        Page* victim = nullptr;
        if (State::manager_.swap_out_victim(mm, &victim, in_tick) != Error::None) {
            cprintf("swap_out: no victim page found\n");
            break;
        }

        if (victim == nullptr) {
            cprintf("swap_out: victim is nullptr\n");
            break;
        }

        uintptr_t victim_addr = find_vaddr_for_page(mm, victim);
        if (victim_addr == 0) {
            cprintf("swap_out: cannot find virtual address for page %p\n", victim);
            continue;
        }

        cprintf("swap_out: swapping out page %p at vaddr 0x%x\n", victim, victim_addr);

        pte_t* ptep = pmm::get_pte(mm->pgdir, victim_addr, 0);
        if (ptep == nullptr) {
            cprintf("swap_out: cannot get PTE for vaddr 0x%x\n", victim_addr);
            continue;
        }

        uintptr_t swap_entry =
            (State::next_offset_ << 8) | ENTRY_HAS_PERMISSIONS | (pte_writable(*ptep) ? ENTRY_WRITE : 0);
        if (swapfs_write(swap_entry, victim) != Error::None) {
            cprintf("swap_out: failed to write to swap\n");
            continue;
        }

        *ptep = swap_entry;

        pmm::invalidate_tlb_page(mm->pgdir, victim_addr);
        pmm::free_pages(victim, 1);

        State::next_offset_++;
        if (State::next_offset_ >= State::max_offset_) {
            State::next_offset_ = 1;  // Wrap around (simple allocation)
        }

        cprintf("swap_out: successfully swapped out page to entry 0x%x\n", swap_entry);
    }

    return i;  // Return number of pages swapped out
}

}  // namespace swap

int swap::swapfs_init() {
    State::device_ = BlockManager::find_device(blk::DeviceType::Disk);
    if (State::device_ == nullptr) {
        cprintf("swapfs init: no disk device found for swap\n");
        return -1;
    }

    cprintf("swapfs init: using device '%s' for swap\n", State::device_->name);
    cprintf("swapfs init: swap starts at sector %d\n", SWAP_START_SECTOR);

    return 0;
}


Error swap::swapfs_read(uintptr_t entry, Page* page) {
    // Calculate disk sector number
    // +--------------------------------+--------+---+
    // |    Swap Offset (24 bits)       | Reserved| P |
    // +--------------------------------+--------+---+
    // Bits 31-8                        Bits 7-1  Bit 0
    uint32_t offset = (entry >> 8) & 0xFFFFFF;  // Extract offset from swap entry
    ENSURE(State::device_ && page && !pte_present(entry) && offset > 0 && offset < State::max_offset_);
    uint32_t sector = SWAP_START_SECTOR + (offset * SECTORS_PER_PAGE);

    void* kva = pmm::page_to_kva(page);
    TRY_LOG(State::device_->read(sector, kva, SECTORS_PER_PAGE), "swapfs_read: disk read failed (sector=%d)", sector);

    cprintf("swapfs_read: read page from swap entry 0x%x (sector %d)\n", entry, sector);
    return Error::None;
}

Error swap::swapfs_write(uintptr_t entry, Page* page) {
    uint32_t offset = (entry >> 8) & 0xFFFFFF;  // Extract offset from swap entry
    ENSURE(State::device_ && page && !pte_present(entry) && offset > 0 && offset < State::max_offset_);
    uint32_t sector = SWAP_START_SECTOR + (offset * SECTORS_PER_PAGE);

    void* kva = pmm::page_to_kva(page);
    TRY_LOG(State::device_->write(sector, kva, SECTORS_PER_PAGE), "swapfs_write: disk write failed (sector=%d)",
            sector);

    cprintf("swapfs_write: wrote page to swap entry 0x%x (sector %d)\n", entry, sector);
    return Error::None;
}
