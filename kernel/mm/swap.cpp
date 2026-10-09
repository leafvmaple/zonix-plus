#include "lib/stdio.h"

#include "swap.h"
#include "pmm.h"

#include <asm/page.h>
#include <asm/mmu.h>
#include "block/blk.h"

namespace {

constexpr uint32_t SWAP_START_LBA = 1000;          // First logical block of swap space
constexpr size_t BLOCKS_PER_PAGE = PG_SIZE / 512;  // Logical blocks needed for one page

class State {
    friend int swap::init();
    friend Error swap::init_mm(MemoryDesc* mm);
    friend Error swap::in(MemoryDesc* mm, uintptr_t va, Page** page_ptr);
    friend int swap::out(MemoryDesc* mm, int page_count, int in_timer_tick);
    friend int swap::swapfs_init();
    friend Error swap::swapfs_read(uintptr_t entry, Page* page);
    friend Error swap::swapfs_write(uintptr_t entry, Page* page);

    inline static SwapManager manager_{};
    inline static unsigned int slot_limit_{};
    inline static BlockDevice* device_{};
    inline static uint32_t next_slot_ = 1;
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
        State::slot_limit_ = 0;
        cprintf("swap: disabled (no swap device)\n");
        return 0;
    }

    if (State::device_->block_count <= SWAP_START_LBA) {
        cprintf("swap: device '%s' too small (%d sectors, need > %d)\n", State::device_->name,
                State::device_->block_count, SWAP_START_LBA);
        State::slot_limit_ = 0;
        return 0;
    }

    uint32_t available_blocks = State::device_->block_count - SWAP_START_LBA;
    State::slot_limit_ = available_blocks / BLOCKS_PER_PAGE;

    if (State::slot_limit_ == 0) {
        cprintf("swap: device '%s' has no usable swap space\n", State::device_->name);
        return 0;
    }

    cprintf("swap: manager=%s, device='%s', %d pages (%d MiB)\n", State::manager_.name, State::device_->name,
            State::slot_limit_, (State::slot_limit_ * PG_SIZE) / (1024 * 1024));

    return 0;
}

Error init_mm(MemoryDesc* mm) {
    return State::manager_.init_mm(mm);
}

Error in(MemoryDesc* mm, uintptr_t va, Page** page_ptr) {
    ENSURE(mm && mm->pgdir && page_ptr);
    *page_ptr = nullptr;
    Page* page = pmm::alloc_pages(1);
    if (page == nullptr) {
        cprintf("swap::in: failed to allocate page\n");
        return Error::NoMem;
    }

    pte_t* ptep = pmm::get_pte(mm->pgdir, va, 0);
    if (ptep == nullptr) {
        cprintf("swap::in: no page table entry\n");
        pmm::free_pages(page, 1);
        return Error::NotFound;
    }

    uintptr_t swap_entry = *ptep;
    if (swap_entry == 0 || pte_present(swap_entry)) {
        pmm::free_pages(page);
        return Error::Invalid;
    }
    if (swapfs_read(swap_entry, page) != Error::None) {
        cprintf("swap::in: failed to read from swap\n");
        pmm::free_pages(page, 1);
        return Error::Io;
    }

    cprintf("swap::in: loaded va 0x%lx from swap entry 0x%lx to page %p\n", static_cast<unsigned long>(va),
            static_cast<unsigned long>(swap_entry), page);

    // Bit 7 marks entries that preserve the original write permission.
    uint32_t perm = user_page_perm((swap_entry & ENTRY_HAS_PERMISSIONS) == 0 || (swap_entry & ENTRY_WRITE) != 0);
    if (pmm::page_insert(mm->pgdir, page, va, perm) != Error::None) {
        pmm::free_pages(page);
        return Error::NoMem;
    }

    State::manager_.map_swappable(mm, va, page, 1);

    *page_ptr = page;
    return Error::None;
}

}  // namespace swap

namespace {

constexpr int LEVEL_SHIFTS[PAGE_LEVELS] = {PML4X_SHIFT, PDPTX_SHIFT, PDX_SHIFT, PTX_SHIFT};

// Recursively walk the page table tree looking for a mapping to target_pa.
// depth indexes LEVEL_SHIFTS; PAGE_LEVELS - 1 is the leaf level.
uintptr_t scan_pt_for_pa(const pde_t* table, int depth, uintptr_t base_va, uintptr_t target_pa) {
    int shift = LEVEL_SHIFTS[depth];
    bool is_leaf = (depth == PAGE_LEVELS - 1);

    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        pde_t entry = table[i];
        if (!pte_present(entry))
            continue;

        uintptr_t va = base_va | (static_cast<uintptr_t>(i) << shift);

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

uintptr_t swap::find_va_for_page(MemoryDesc* mm, Page* page) {
    return scan_pt_for_pa(mm->pgdir, 0, 0, pmm::page_to_phys(page));
}

namespace swap {

int out(MemoryDesc* mm, int page_count, int in_timer_tick) {
    int attempt_count{};

    for (attempt_count = 0; attempt_count < page_count; attempt_count++) {
        Page* victim = nullptr;
        if (State::manager_.swap_out_victim(mm, &victim, in_timer_tick) != Error::None) {
            cprintf("swap::out: no victim page found\n");
            break;
        }

        if (victim == nullptr) {
            cprintf("swap::out: victim is nullptr\n");
            break;
        }

        uintptr_t victim_va = find_va_for_page(mm, victim);
        if (victim_va == 0) {
            cprintf("swap::out: cannot find virtual address for page %p\n", victim);
            continue;
        }

        cprintf("swap::out: swapping out page %p at va 0x%lx\n", victim, static_cast<unsigned long>(victim_va));

        pte_t* ptep = pmm::get_pte(mm->pgdir, victim_va, 0);
        if (ptep == nullptr) {
            cprintf("swap::out: cannot get PTE for va 0x%lx\n", static_cast<unsigned long>(victim_va));
            continue;
        }

        uintptr_t swap_entry =
            (State::next_slot_ << 8) | ENTRY_HAS_PERMISSIONS | (pte_writable(*ptep) ? ENTRY_WRITE : 0);
        if (swapfs_write(swap_entry, victim) != Error::None) {
            cprintf("swap::out: failed to write to swap\n");
            continue;
        }

        *ptep = swap_entry;

        pmm::invalidate_tlb_page(mm->pgdir, victim_va);
        pmm::free_pages(victim, 1);

        State::next_slot_++;
        if (State::next_slot_ >= State::slot_limit_) {
            State::next_slot_ = 1;  // Wrap around (simple allocation)
        }

        cprintf("swap::out: successfully swapped out page to entry 0x%lx\n", static_cast<unsigned long>(swap_entry));
    }

    return attempt_count;  // Completed loop iterations; mapping or write failures can also be counted.
}

}  // namespace swap

int swap::swapfs_init() {
    State::device_ = BlockManager::find_device(blk::DeviceType::Disk);
    if (State::device_ == nullptr) {
        cprintf("swapfs_init: no disk device found for swap\n");
        return -1;
    }

    cprintf("swapfs_init: using device '%s' for swap\n", State::device_->name);
    cprintf("swapfs_init: swap starts at LBA %d\n", SWAP_START_LBA);

    return 0;
}


Error swap::swapfs_read(uintptr_t entry, Page* page) {
    // Decode the swap slot, then calculate its first LBA.
    // +--------------------------------+--------+---+
    // |    Swap Slot (24 bits)         | Perms   | P |
    // +--------------------------------+--------+---+
    // Bits 31-8: slot; bit 7: permissions saved; bit 1: writable; bit 0: present.
    uint32_t slot = (entry >> 8) & 0xFFFFFF;  // Extract the page-sized swap slot index
    ENSURE(State::device_ && page && !pte_present(entry) && slot > 0 && slot < State::slot_limit_);
    uint32_t start_lba = SWAP_START_LBA + (slot * BLOCKS_PER_PAGE);

    void* kva = pmm::page_to_kva(page);
    TRY_LOG(State::device_->read(start_lba, kva, BLOCKS_PER_PAGE), "swapfs_read: disk read failed (start_lba=%d)",
            start_lba);

    cprintf("swapfs_read: read page from swap entry 0x%lx (LBA %d)\n", static_cast<unsigned long>(entry), start_lba);
    return Error::None;
}

Error swap::swapfs_write(uintptr_t entry, Page* page) {
    uint32_t slot = (entry >> 8) & 0xFFFFFF;  // Extract the page-sized swap slot index
    ENSURE(State::device_ && page && !pte_present(entry) && slot > 0 && slot < State::slot_limit_);
    uint32_t start_lba = SWAP_START_LBA + (slot * BLOCKS_PER_PAGE);

    void* kva = pmm::page_to_kva(page);
    TRY_LOG(State::device_->write(start_lba, kva, BLOCKS_PER_PAGE), "swapfs_write: disk write failed (start_lba=%d)",
            start_lba);

    cprintf("swapfs_write: wrote page to swap entry 0x%lx (LBA %d)\n", static_cast<unsigned long>(entry), start_lba);
    return Error::None;
}
