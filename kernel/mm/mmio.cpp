#include "mmio.h"
#include "vmm.h"
#include "drivers/intr.h"
#include <asm/mmu.h>
#include <asm/arch.h>

namespace vmm {

class MmioAllocator {
public:
    static Result<MmioRegion> map(uintptr_t pa, size_t byte_count, uint32_t perm) {
        ENSURE(byte_count != 0, Error::Invalid);
        size_t offset = pa & (PG_SIZE - 1);
        size_t span{};
        ENSURE(!__builtin_add_overflow(byte_count, offset, &span) && !__builtin_add_overflow(span, PG_SIZE - 1, &span),
               Error::Invalid);
        span &= ~(PG_SIZE - 1);
        uintptr_t end_pa{};
        ENSURE(!__builtin_add_overflow(pa - offset, span, &end_pa), Error::Invalid);
        MmioRegion region(new (sys::nothrow) MmioMapping{});
        ENSURE(region, Error::NoMem);
        intr::Guard guard;
        ENSURE(Manager::kernel_pgdir(), Error::NoMem);
        ListNode* insertion = &mappings_;
        uintptr_t va = KERNEL_DEVIO_BASE;
        for (auto* node : mappings_) {
            auto* current = node->container<MmioMapping>();
            if (current->va_ - va >= span) {
                insertion = node;
                break;
            }
            va = current->va_ + current->span_;
        }
        uintptr_t end_va{};
        ENSURE(!__builtin_add_overflow(va, span, &end_va), Error::NoMem);
        // The list records both owned and permanent mappings. First fit reuses
        // holes without allocating bookkeeping during cleanup.
        size_t mapped = 0;
        for (; mapped < span; mapped += PG_SIZE) {
            auto* pte = pmm::get_pte(Manager::kernel_pgdir(), va + mapped, true);
            if (!pte) {
                // Include the failed walk: it can contain newly allocated tables.
                unmap(va, mapped + PG_SIZE);
                return Error::NoMem;
            }
            assert(!pte_present(*pte));
            *pte = make_pte_page(pa - offset + mapped, perm);
        }
        arch_invalidate_tlb_range(va, span);
        region->va_ = va;
        region->span_ = span;
        region->offset_ = offset;
        region->size_ = byte_count;
        insertion->add_before(region->node_);
        return sys::move(region);
    }

    static void release(MmioMapping* mapping) {
        if (!mapping) {
            return;
        }
        intr::Guard guard;
        if (mapping->span_ != 0) {
            mapping->node_.unlink();
            unmap(mapping->va_, mapping->span_);
        }
        delete mapping;
    }

private:
    static void unmap(uintptr_t va, size_t span) {
        for (size_t offset = 0; offset < span; offset += PG_SIZE) {
            pmm::unmap_mmio_page(Manager::kernel_pgdir(), va + offset);
        }
        arch_invalidate_tlb_range(va, span);
    }
    inline static ListNode mappings_{};
};

void MmioUnmap::operator()(MmioMapping* mapping) const {
    MmioAllocator::release(mapping);
}

Result<MmioRegion> map_mmio(uintptr_t pa, size_t byte_count, uint32_t perm) {
    return MmioAllocator::map(pa, byte_count, perm);
}

uintptr_t mmio_map(uintptr_t pa, size_t byte_count, uint32_t perm) {
    auto mapped = map_mmio(pa, byte_count, perm);
    if (!mapped.ok()) {
        return 0;
    }
    auto region = mapped.release_value();
    uintptr_t address = region->address();
    // Legacy boot callers request permanent mappings. The allocator retains the
    // record for the kernel lifetime, so subsequent owners cannot overlap it.
    (void)region.release();
    return address;
}

}  // namespace vmm
