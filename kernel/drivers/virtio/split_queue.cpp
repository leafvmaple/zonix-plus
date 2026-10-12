#include "split_queue.h"
#include <asm/arch.h>
#include <asm/page.h>
#include <asm/mmu.h>

namespace virtio {

Error SplitQueue::allocate(uint16_t size) {
    ENSURE(size_ == 0, Error::Busy);
    ENSURE(size != 0 && size <= 32768 && (size & (size - 1)) == 0);
    auto descriptors = TRY(KernelBuffer::alloc(sizeof(Descriptor) * size));
    auto available = TRY(KernelBuffer::alloc(sizeof(uint16_t) * (3 + size)));
    auto used = TRY(KernelBuffer::alloc(sizeof(uint16_t) * 3 + sizeof(Completion) * size));
    memset(descriptors.data(), 0, descriptors.size());
    memset(available.data(), 0, available.size());
    memset(used.data(), 0, used.size());
    descriptors_ = sys::move(descriptors);
    available_ = sys::move(available);
    used_ = sys::move(used);
    size_ = size;
    last_used_ = 0;
    return Error::None;
}

Error SplitQueue::set_buffer(uint16_t id, uintptr_t pa, uint32_t length, bool device_writes) {
    ENSURE(id < size_ && length != 0);
    constexpr uint16_t device_write = 2;
    auto* descriptors = reinterpret_cast<Descriptor*>(descriptors_.data());
    descriptors[id] = {pa, length, static_cast<uint16_t>(device_writes ? device_write : 0), 0};
    return Error::None;
}

Error SplitQueue::submit(uint16_t id) {
    ENSURE(id < size_);
    auto* available = reinterpret_cast<Available*>(available_.data());
    available->ring[available->index % size_] = id;
    // Complete prior buffer reads before returning ownership to the device;
    // also publish the descriptor and ring entry before the available index.
    arch_mb();
    available->index++;
    arch_wmb();
    return Error::None;
}

Result<Completion> SplitQueue::pop_used() {
    ENSURE(size_ != 0);
    auto* used = reinterpret_cast<volatile Used*>(used_.data());
    uint16_t pending = static_cast<uint16_t>(used->index - last_used_);
    ENSURE(pending != 0, Error::NotFound);
    ENSURE(pending <= size_);
    arch_mb();
    const auto& entry = used->ring[last_used_ % size_];
    Completion completion{entry.id, entry.length};
    ENSURE(completion.id < size_);
    ++last_used_;
    return completion;
}

void SplitQueue::release() {
    descriptors_ = KernelBuffer{};
    available_ = KernelBuffer{};
    used_ = KernelBuffer{};
    size_ = last_used_ = 0;
}

uintptr_t SplitQueue::descriptor_pa() const {
    return virt_to_phys(descriptors_.data());
}
uintptr_t SplitQueue::available_pa() const {
    return virt_to_phys(available_.data());
}
uintptr_t SplitQueue::used_pa() const {
    return virt_to_phys(used_.data());
}

}  // namespace virtio
