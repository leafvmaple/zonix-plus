#include "swap.h"
#include <kernel/log.h>
#include "block/blk.h"
#include "drivers/intr.h"
#include "lib/kernel_buffer.h"
#include "lib/mutex.h"
#include "lib/stdio.h"
#include "sched/sched.h"
#include <asm/mmu.h>
#include <sys/algorithm.hpp>
#include <sys/mutex.hpp>

namespace {

constexpr size_t BLOCKS_PER_PAGE = PG_SIZE / BlockDevice::BLOCK_SIZE_BYTES;
constexpr uintptr_t ENTRY_SLOT_MASK = (swap::MAX_OFFSET_LIMIT - 1) << 8;
constexpr uintptr_t ENTRY_FLAGS = swap::ENTRY_HAS_PERMISSIONS | swap::ENTRY_WRITE | swap::ENTRY_EXECUTE;

class State {
    friend int swap::init();
    friend Error swap::init_mm(MemoryDesc* mm);
    friend Error swap::in(MemoryDesc* mm, uintptr_t va, Page** page_ptr);
    friend int swap::out(MemoryDesc* mm, int page_count, int in_timer_tick);
    friend int swap::swapfs_init();
    friend Error swap::swapfs_read(uintptr_t entry, Page* page);
    friend Error swap::swapfs_write(uintptr_t entry, Page* page);
    friend void swap::release_entry(uintptr_t entry);

    static bool occupied(uint32_t slot) { return (slots_.data()[slot / 8] & (1U << (slot % 8))) != 0; }
    static void set_occupied(uint32_t slot, bool value) {
        uint8_t mask = static_cast<uint8_t>(1U << (slot % 8));
        if (value) {
            slots_.data()[slot / 8] |= mask;
        } else {
            slots_.data()[slot / 8] &= static_cast<uint8_t>(~mask);
        }
    }
    static uint32_t allocated_slot(uintptr_t entry) {
        intr::Guard guard;
        if (!device_ || device_->type != blk::DeviceType::Swap || slots_.empty() ||
            (entry & ~(ENTRY_SLOT_MASK | ENTRY_FLAGS)) != 0 || (entry & swap::ENTRY_HAS_PERMISSIONS) == 0) {
            return 0;
        }
        uint32_t slot = static_cast<uint32_t>(entry >> 8);
        return slot > 0 && slot < slot_limit_ && occupied(slot) ? slot : 0;
    }
    static Result<uint32_t> reserve_slot() {
        intr::Guard guard;
        ENSURE(device_ && device_->type == blk::DeviceType::Swap && !slots_.empty(), Error::NoDevice);
        for (uint32_t checked = 1; checked < slot_limit_; ++checked) {
            uint32_t slot = next_slot_;
            if (++next_slot_ == slot_limit_) {
                next_slot_ = 1;
            }
            if (!occupied(slot)) {
                set_occupied(slot, true);
                return slot;
            }
        }
        return Error::Full;
    }

    inline static SwapManager manager_{};
    inline static Mutex io_lock_{};
    inline static KernelBuffer slots_{};
    inline static uint32_t slot_limit_{};
    inline static BlockDevice* device_{};
    inline static uint32_t next_slot_ = 1;
};

constexpr int LEVEL_SHIFTS[PAGE_LEVELS] = {PML4X_SHIFT, PDPTX_SHIFT, PDX_SHIFT, PTX_SHIFT};

uintptr_t scan_pt_for_pa(const pde_t* table, int depth, uintptr_t base_va, uintptr_t target_pa) {
    int shift = LEVEL_SHIFTS[depth];
    bool is_leaf = depth == PT_WALK_LEVELS - 1;
    int entries = depth == 0 ? USER_TOP_ENTRIES : PAGE_TABLE_ENTRIES;
    for (int i = 0; i < entries; ++i) {
        pde_t entry = table[i];
        if (!pte_present(entry)) {
            continue;
        }
        uintptr_t va = base_va | (static_cast<uintptr_t>(i) << shift);
        if (is_leaf) {
            if (pte_addr(entry) == target_pa) {
                return va;
            }
        } else if (!pte_is_block(entry)) {
            uintptr_t result = scan_pt_for_pa(phys_to_virt<pde_t>(pte_addr(entry)), depth + 1, va, target_pa);
            if (result != 0) {
                return result;
            }
        }
    }
    return 0;
}

}  // namespace

namespace swap {

int init() {
    if (State::manager_.init() != Error::None) {
        return -1;
    }
    (void)swapfs_init();
    return 0;
}

int swapfs_init() {
    sys::lock_guard<Mutex> guard(State::io_lock_);
    if (State::device_) {
        return 0;
    }
    // Only an explicitly dedicated device may be overwritten. Ordinary disks
    // and their filesystem partitions are never selected as a fallback.
    auto* device = BlockManager::find_device(blk::DeviceType::Swap);
    if (!device) {
        klog::debug("swap: disabled (no dedicated swap device)\n");
        return -1;
    }
    uint32_t slot_limit = static_cast<uint32_t>(
        sys::min(static_cast<size_t>(device->block_count / BLOCKS_PER_PAGE) + 1, MAX_OFFSET_LIMIT));
    if (slot_limit <= 1) {
        klog::warn("swap: device '%s' is too small; swap disabled\n", device->name);
        return -1;
    }
    auto allocation = KernelBuffer::alloc((slot_limit + 7) / 8);
    if (!allocation.ok()) {
        klog::warn("swap: slot allocation failed: %s; swap disabled\n", error_str(allocation.error()));
        return -1;
    }
    auto slots = allocation.release_value();
    memset(slots.data(), 0, slots.size());
    State::slots_ = sys::move(slots);
    State::slot_limit_ = slot_limit;
    State::next_slot_ = 1;
    State::device_ = device;
    klog::info("swap: dedicated device '%s', %u page slots\n", device->name, slot_limit - 1);
    return 0;
}

Error init_mm(MemoryDesc* mm) {
    return State::manager_.init_mm(mm);
}

Error in(MemoryDesc* mm, uintptr_t va, Page** page_ptr) {
    ENSURE(mm && mm->pgdir && mm->pgdir != vmm::Manager::kernel_pgdir() && page_ptr && va >= PG_SIZE &&
           va < USER_SPACE_TOP);
    *page_ptr = nullptr;
    sys::lock_guard<Mutex> guard(State::io_lock_);
    ENSURE(State::device_, Error::NoDevice);
    pte_t* ptep = pmm::get_pte(mm->pgdir, va, false);
    ENSURE(ptep, Error::NotFound);
    uintptr_t entry = *ptep;
    ENSURE(!pte_present(entry) && State::allocated_slot(entry) != 0);
    Page* page = pmm::alloc_pages();
    ENSURE(page, Error::NoMem);
    Error error = swapfs_read(entry, page);
    if (error != Error::None) {
        pmm::free_pages(page);
        return error;
    }
    intr::Guard irq_guard;
    if (*ptep != entry) {
        pmm::free_pages(page);
        return Error::Busy;
    }
    uint32_t perm = user_page_perm((entry & ENTRY_WRITE) != 0, (entry & ENTRY_EXECUTE) != 0);
    error = State::manager_.map_swappable(mm, va, page, 1);
    if (error == Error::None) {
        error = pmm::page_insert(mm->pgdir, page, va, perm);
    }
    if (error != Error::None) {
        page->node().unlink();
        pmm::free_pages(page);
        return error;
    }
    release_entry(entry);
    *page_ptr = page;
    return Error::None;
}

uintptr_t find_va_for_page(MemoryDesc* mm, Page* page) {
    if (!mm || !mm->pgdir || !page) {
        return 0;
    }
    return scan_pt_for_pa(mm->pgdir, 0, 0, pmm::page_to_phys(page));
}

int out(MemoryDesc* mm, int page_count, int in_timer_tick) {
    // Disk I/O is not allowed from a timer interrupt. Shared frames and kernel
    // address spaces need a different eviction protocol and are excluded here.
    // Only reclaim the caller's MM: its user code cannot modify the frame while
    // this task waits for I/O. Background eviction needs page freezing first.
    Task* current = sched::current();
    if (!mm || !mm->pgdir || mm->pgdir == vmm::Manager::kernel_pgdir() || !current || current->memory() != mm ||
        page_count <= 0 || in_timer_tick != 0) {
        return 0;
    }
    sys::lock_guard<Mutex> guard(State::io_lock_);
    if (!State::device_ || State::slots_.empty()) {
        return 0;
    }
    int swapped = 0;
    for (; swapped < page_count; ++swapped) {
        Page* victim = nullptr;
        if (State::manager_.swap_out_victim(mm, &victim, 0) != Error::None) {
            break;
        }
        uintptr_t va = find_va_for_page(mm, victim);
        pte_t* ptep = va != 0 ? pmm::get_pte(mm->pgdir, va, false) : nullptr;
        auto reserved = State::reserve_slot();
        if (!ptep || !pte_present(*ptep) || !pte_user(*ptep) || victim->ref_count != 1 || !reserved.ok()) {
            if (reserved.ok()) {
                release_entry((static_cast<uintptr_t>(reserved.value()) << 8) | ENTRY_HAS_PERMISSIONS);
            }
            intr::Guard irq_guard;
            mm->swap_list.add(victim->node());
            break;
        }
        uintptr_t resident = *ptep;
        uintptr_t entry = (static_cast<uintptr_t>(reserved.value()) << 8) | ENTRY_HAS_PERMISSIONS |
                          (pte_writable(resident) ? ENTRY_WRITE : 0) | (pte_executable(resident) ? ENTRY_EXECUTE : 0);
        Error error = swapfs_write(entry, victim);
        intr::Guard irq_guard;
        if (error != Error::None || *ptep != resident || victim->ref_count != 1) {
            release_entry(entry);
            mm->swap_list.add(victim->node());
            break;
        }
        *ptep = entry;
        pmm::invalidate_tlb_page(mm->pgdir, va);
        --victim->ref_count;
        pmm::free_pages(victim);
    }
    return swapped;
}

void release_entry(uintptr_t entry) {
    intr::Guard guard;
    uint32_t slot = State::allocated_slot(entry);
    if (slot != 0) {
        State::set_occupied(slot, false);
    }
}

Error swapfs_read(uintptr_t entry, Page* page) {
    ENSURE(State::device_, Error::NoDevice);
    uint32_t slot = State::allocated_slot(entry);
    ENSURE(page && slot != 0);
    uint32_t start_lba = static_cast<uint32_t>((slot - 1) * BLOCKS_PER_PAGE);
    return State::device_->read(start_lba, pmm::page_to_kva(page), BLOCKS_PER_PAGE);
}

Error swapfs_write(uintptr_t entry, Page* page) {
    ENSURE(State::device_, Error::NoDevice);
    uint32_t slot = State::allocated_slot(entry);
    ENSURE(page && slot != 0);
    uint32_t start_lba = static_cast<uint32_t>((slot - 1) * BLOCKS_PER_PAGE);
    return State::device_->write(start_lba, pmm::page_to_kva(page), BLOCKS_PER_PAGE);
}

}  // namespace swap
