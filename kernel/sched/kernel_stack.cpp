#include "kernel_stack.h"

#include "drivers/intr.h"
#include "lib/memory.h"
#include "mm/page_table.h"
#include "mm/vmm.h"
#include <asm/arch.h>
#include <asm/mmu.h>
#include <kernel/vm_flags.h>

namespace {

// Maps or unmaps one page of the kernel's direct mapping. The page tables
// below the root are shared by every address space, so the change applies to
// all of them. Where the direct mapping still uses a large block, the walk
// splits it into pages first, which can fail for lack of a table page.
bool set_mapped(uintptr_t page, bool mapped) {
    intr::Guard guard;
    pte_t* entry = pmm::get_pte(vmm::Manager::kernel_pgdir(), page, true);
    if (!entry) {
        return false;
    }
    *entry = mapped ? make_pte_page(virt_to_phys(page), VM_WRITE) : 0;
    arch_invalidate_tlb_range(page, PG_SIZE);
    return true;
}

}  // namespace

Result<KernelStack> KernelStack::alloc() {
    auto block = reinterpret_cast<uintptr_t>(kmalloc(SIZE + PG_SIZE));
    ENSURE(block != 0, Error::NoMem);
    // Whole pages come straight from the page allocator, so they are page aligned.
    assert((block & PG_MASK) == 0);
    if (!set_mapped(block, false)) {
        kfree(reinterpret_cast<void*>(block));
        return Error::NoMem;
    }
    return KernelStack(block);
}

void KernelStack::release() {
    if (block_ == 0) {
        return;
    }
    // The allocator's next owner expects the whole block to be mapped.
    static_cast<void>(set_mapped(block_, true));
    kfree(reinterpret_cast<void*>(block_));
    block_ = 0;
}
