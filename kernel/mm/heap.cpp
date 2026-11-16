#include "heap.h"
#include "pmm.h"
#include "drivers/intr.h"
#include "debug/assert.h"
#include "lib/memory.h"
#include <asm/mmu.h>
#include <sys/array.hpp>

namespace {

constexpr size_t MIN_OBJECT_SIZE = 16;
constexpr size_t MAX_OBJECT_SIZE = 1024;
constexpr size_t CLASS_COUNT = 7;
constexpr size_t BITMAP_WORD_BITS = 64;
constexpr size_t BITMAP_WORD_COUNT = PG_SIZE / MIN_OBJECT_SIZE / BITMAP_WORD_BITS;

struct FreeSlot {
    FreeSlot* next{};
};

// Each slab belongs to one size class. Its bitmap detects invalid/double frees;
// its free list provides constant-time slot allocation without another owner.
struct SlabPage {
    sys::list_node<SlabPage> partial_node{*this};
    FreeSlot* free_slot{};
    sys::array<uint64_t, BITMAP_WORD_COUNT> allocated{};
    uint16_t object_size{};
    uint16_t object_offset{};
    uint16_t object_count{};
    uint16_t live_count{};
};

static_assert(MIN_OBJECT_SIZE >= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
static_assert(sizeof(FreeSlot) <= MIN_OBJECT_SIZE);
static_assert(MAX_OBJECT_SIZE == (MIN_OBJECT_SIZE << (CLASS_COUNT - 1)));
static_assert(PG_SIZE % (MIN_OBJECT_SIZE * BITMAP_WORD_BITS) == 0);
static_assert(PG_SIZE <= 65535);
static_assert(sizeof(SlabPage) <= MAX_OBJECT_SIZE);
static_assert(MAX_OBJECT_SIZE * 2 <= PG_SIZE);

using SlabList = sys::intrusive_list<SlabPage, &SlabPage::partial_node>;

class KernelHeap {
public:
    [[nodiscard]] static void* alloc(size_t byte_count);
    static void free(void* ptr);

private:
    [[nodiscard]] static SlabPage* create_slab(size_t class_index, size_t object_size);
    [[nodiscard]] static void* alloc_small(size_t class_index, size_t object_size);
    static void free_small(Page* page, void* ptr);

    // Only slabs with an available slot are linked. Full slabs are found from
    // the pointer's Page descriptor when freed. Empty slabs return immediately.
    inline static sys::array<SlabList, CLASS_COUNT> partial_{};
};

SlabPage* KernelHeap::create_slab(size_t class_index, size_t object_size) {
    Page* page = pmm::alloc_pages();
    if (!page) {
        return nullptr;
    }
    page->set_heap();
    page->set_slab();
    auto* slab = new (pmm::page_to_kva(page)) SlabPage();
    slab->object_size = object_size;
    slab->object_offset = (sizeof(SlabPage) + object_size - 1) & ~(object_size - 1);
    slab->object_count = (PG_SIZE - slab->object_offset) / object_size;
    auto* storage = reinterpret_cast<uint8_t*>(slab) + slab->object_offset;
    for (size_t i = slab->object_count; i != 0; --i) {
        auto* slot = new (storage + (i - 1) * object_size) FreeSlot{slab->free_slot};
        slab->free_slot = slot;
    }
    partial_[class_index].push_front(*slab);
    return slab;
}

void* KernelHeap::alloc_small(size_t class_index, size_t object_size) {
    auto& partial = partial_[class_index];
    SlabPage* slab = partial.empty() ? create_slab(class_index, object_size) : &partial.front();
    if (!slab) {
        return nullptr;
    }
    FreeSlot* slot = slab->free_slot;
    slab->free_slot = slot->next;
    size_t offset = reinterpret_cast<uintptr_t>(slot) - reinterpret_cast<uintptr_t>(slab);
    size_t index = (offset - slab->object_offset) / slab->object_size;
    slab->allocated[index / BITMAP_WORD_BITS] |= uint64_t{1} << (index % BITMAP_WORD_BITS);
    ++slab->live_count;
    if (!slab->free_slot) {
        slab->partial_node.unlink();
    }
    slot->~FreeSlot();
    return slot;
}

void* KernelHeap::alloc(size_t byte_count) {
    if (byte_count == 0) {
        return nullptr;
    }
    // The entire heap operation is atomic on the current single-CPU kernel.
    // Neither heap nor PMM allocation sleeps or recursively allocates metadata.
    intr::Guard guard;
    if (byte_count <= MAX_OBJECT_SIZE) {
        size_t object_size = MIN_OBJECT_SIZE;
        size_t class_index{};
        while (object_size < byte_count) {
            object_size <<= 1;
            ++class_index;
        }
        return alloc_small(class_index, object_size);
    }
    // Subtract before rounding: even SIZE_MAX cannot wrap into a small request.
    size_t page_count = 1 + (byte_count - 1) / PG_SIZE;
    Page* page = pmm::alloc_pages(page_count);
    if (!page) {
        return nullptr;
    }
    page->set_heap();
    return pmm::page_to_kva(page);
}

void KernelHeap::free_small(Page* page, void* ptr) {
    auto* slab = static_cast<SlabPage*>(pmm::page_to_kva(page));
    size_t offset = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(slab);
    assert(offset >= slab->object_offset);
    size_t index = (offset - slab->object_offset) / slab->object_size;
    assert(index < slab->object_count && (offset - slab->object_offset) % slab->object_size == 0);
    uint64_t mask = uint64_t{1} << (index % BITMAP_WORD_BITS);
    auto& word = slab->allocated[index / BITMAP_WORD_BITS];
    assert((word & mask) != 0 && slab->live_count != 0);
    word &= ~mask;
    bool was_full = slab->free_slot == nullptr;
    slab->free_slot = new (ptr) FreeSlot{slab->free_slot};
    --slab->live_count;
    if (slab->live_count == 0) {
        slab->partial_node.unlink();
        slab->~SlabPage();
        pmm::free_pages(page);
    } else if (was_full) {
        size_t object_size = MIN_OBJECT_SIZE;
        size_t class_index{};
        while (object_size < slab->object_size) {
            object_size <<= 1;
            ++class_index;
        }
        partial_[class_index].push_front(*slab);
    }
}

void KernelHeap::free(void* ptr) {
    if (!ptr) {
        return;
    }
    intr::Guard guard;
    uintptr_t va = reinterpret_cast<uintptr_t>(ptr);
    assert(va >= KERNEL_BASE && (virt_to_phys(ptr) >> PG_SHIFT) < pmm::page_count());
    Page* page = pmm::kva_to_page(ptr);
    assert(page->is_heap() && !page->is_reserved());
    if (page->is_slab()) {
        free_small(page, ptr);
    } else {
        assert(ptr == pmm::page_to_kva(page) && page->block_page_count != 0);
        pmm::free_pages(page, page->block_page_count);
    }
}

}  // namespace

void* kmalloc(size_t byte_count) {
    return KernelHeap::alloc(byte_count);
}

void kfree(void* ptr) {
    KernelHeap::free(ptr);
}
