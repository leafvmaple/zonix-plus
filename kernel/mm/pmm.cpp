#include "pmm.h"
#include <kernel/log.h>
#include "debug/assert.h"
#include "drivers/intr.h"

#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/math.h"

#include <asm/arch.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <kernel/boot_info.h>

// Declared in head.S — kernel's own copy of boot_info
extern struct BootInfo __kernel_boot_info;

// Pages are reached through the direct map, so available RAM beyond it is
// left out; firmware may report far more memory than the kernel maps.
template<typename F>
void traverse_boot_mmap(F&& callback) {
    struct BootInfo* bi = &__kernel_boot_info;
    auto* entries = reinterpret_cast<struct BootMemEntry*>(bi->mmap_pa + KERNEL_BASE);
    uint32_t count = bi->mmap_count;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t base_pa = entries[i].base_pa;
        uint64_t size_bytes = entries[i].size_bytes;
        if (entries[i].type == BOOT_MEM_AVAILABLE) {
            if (base_pa >= KERNEL_DIRECT_MAP_END_PA)
                continue;
            if (size_bytes > KERNEL_DIRECT_MAP_END_PA - base_pa)
                size_bytes = KERNEL_DIRECT_MAP_END_PA - base_pa;
        }
        callback(base_pa, size_bytes, entries[i].type);
    }
}

// Page management constants and bootstrap helpers
namespace {

class Factory {
public:
    static int init() {
        allocator_.init();
        klog::debug("pmm: allocator = %s\n", allocator_.name());
        return 0;
    }

    static PageAllocator& allocator() { return allocator_; }
    static Page* page_descriptors() { return page_desc_; }
    static uint32_t page_count() { return page_count_; }
    static void set_page_descriptors(Page* pages, uint32_t count) {
        page_desc_ = pages;
        page_count_ = count;
    }

private:
    inline static PageAllocator allocator_{};
    inline static Page* page_desc_{};
    inline static uint32_t page_count_{};
};

static bool normalize_available_range(uint64_t base_pa, uint64_t size_bytes, uintptr_t min_pa, uint64_t* out_begin,
                                      uint64_t* out_end) {
    if (size_bytes == 0)
        return false;

    uint64_t limit{};
    if (__builtin_add_overflow(base_pa, size_bytes, &limit)) {
        limit = static_cast<uint64_t>(-1);
    }

    uint64_t begin = (base_pa < min_pa) ? static_cast<uint64_t>(min_pa) : base_pa;
    if (begin >= limit)
        return false;

    begin = round_up(begin, PG_SIZE);
    limit = round_down(limit, PG_SIZE);
    if (begin >= limit)
        return false;

    *out_begin = begin;
    *out_end = limit;
    return true;
}

}  // namespace

uintptr_t pmm::page_to_phys(Page* page) {
    return static_cast<uintptr_t>((page - Factory::page_descriptors()) << PG_SHIFT);
}

void* pmm::page_to_kva(Page* page) {
    return reinterpret_cast<void*>(KERNEL_BASE + pmm::page_to_phys(page));
}

Page* pmm::phys_to_page(uintptr_t pa) {
    size_t page_index = pa >> PG_SHIFT;
    return Factory::page_descriptors() + page_index;
}

Page* pmm::kva_to_page(void* kva) {
    return pmm::phys_to_page(virt_to_phys(kva));
}

Page* pmm::alloc_pages(size_t page_count /*= 1*/) {
    intr::Guard guard;
    return Factory::allocator().alloc(page_count);
}

void pmm::free_pages(Page* base, size_t page_count /*= 1*/) {
    intr::Guard guard;
    Factory::allocator().free(base, page_count);
}

static int page_init() {
    BootInfo* bi = &__kernel_boot_info;

    if (bi->mmap_count == 0) {
        klog::error("pmm: boot memory map is empty\n");
        return -1;
    }

    uint64_t unmapped_bytes{};
    auto* entries = reinterpret_cast<struct BootMemEntry*>(bi->mmap_pa + KERNEL_BASE);
    for (uint32_t i = 0; i < bi->mmap_count; i++) {
        uint64_t end = entries[i].base_pa + entries[i].size_bytes;
        if (entries[i].type == BOOT_MEM_AVAILABLE && end > KERNEL_DIRECT_MAP_END_PA)
            unmapped_bytes +=
                end - (entries[i].base_pa > KERNEL_DIRECT_MAP_END_PA ? entries[i].base_pa : KERNEL_DIRECT_MAP_END_PA);
    }
    if (unmapped_bytes != 0)
        klog::info("memory: %lu MiB above the direct map left unused\n", unmapped_bytes / (1024 * 1024));

    uint64_t max_pa{};
    traverse_boot_mmap([&max_pa](uint64_t base_pa, uint64_t size_bytes, uint32_t type) {
        if (type == BOOT_MEM_AVAILABLE) {
            uint64_t limit{};
            if (__builtin_add_overflow(base_pa, size_bytes, &limit)) {
                limit = static_cast<uint64_t>(-1);
            }
            max_pa = (limit > max_pa) ? limit : max_pa;
        }
    });

    if (max_pa == 0) {
        klog::error("pmm: no usable memory regions in boot map (%d entries)\n", bi->mmap_count);
        return -1;
    }

    extern uint8_t KERNEL_END[];

    uint32_t page_count = round_up(max_pa, PG_SIZE) / PG_SIZE;
    if (page_count == 0) {
        klog::error("pmm: max physical address too small (0x%lx)\n", static_cast<uint64_t>(max_pa));
        return -1;
    }

    Factory::set_page_descriptors(reinterpret_cast<Page*>(round_up(reinterpret_cast<void*>(KERNEL_END), PG_SIZE)),
                                  page_count);

    klog::debug("pmm: %d pages, page array at [0x%p], max_pa=0x%lx\n", Factory::page_count(),
                Factory::page_descriptors(), static_cast<uint64_t>(max_pa));

    for (uint32_t i = 0; i < Factory::page_count(); i++) {
        auto* page = new (&Factory::page_descriptors()[i]) Page();
        page->set_reserved();
    }

    uintptr_t valid_mem =
        virt_to_phys(reinterpret_cast<uintptr_t>(Factory::page_descriptors() + Factory::page_count()));
    traverse_boot_mmap([valid_mem](uint64_t base_pa, uint64_t size_bytes, uint32_t type) {
        if (type != BOOT_MEM_AVAILABLE)
            return;

        uint64_t begin{};
        uint64_t limit{};
        if (!normalize_available_range(base_pa, size_bytes, valid_mem, &begin, &limit))
            return;

        klog::debug("pmm: free region [0x%016lx, 0x%016lx]\n", static_cast<uint64_t>(begin),
                    static_cast<uint64_t>(limit));
        Factory::allocator().init_memmap(pmm::phys_to_page(begin), (limit - begin) / PG_SIZE);
    });

    return 0;
}

size_t pmm::free_page_count() {
    return Factory::allocator().free_page_count();
}

size_t pmm::page_count() {
    return Factory::page_count();
}

int pmm::init() {
    Factory::init();

    int rc = page_init();
    if (rc != 0) {
        klog::error("pmm: page_init failed (rc=%d)\n", rc);
        return rc;
    }

    size_t free_pages = Factory::allocator().free_page_count();
    if (free_pages == 0) {
        klog::error("pmm: no free pages after initialization\n");
        return -1;
    }

    klog::info("memory: %d free pages (%d MiB)\n", static_cast<int>(free_pages),
               static_cast<int>((free_pages * PG_SIZE) / (1024ULL * 1024)));
    return 0;
}
