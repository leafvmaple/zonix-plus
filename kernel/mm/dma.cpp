#include "mm/dma.h"
#include "lib/memory.h"
#include "mm/pmm.h"

#include <asm/page.h>
#include <sys/utility.hpp>

DmaBuffer::DmaBuffer(DmaBuffer&& other) noexcept
    : pages_(sys::exchange(other.pages_, nullptr)),
      page_count_(sys::exchange(other.page_count_, 0)),
      data_(sys::exchange(other.data_, nullptr)),
      pa_(sys::exchange(other.pa_, 0)),
      size_(sys::exchange(other.size_, 0)) {}

DmaBuffer& DmaBuffer::operator=(DmaBuffer&& other) noexcept {
    if (this != &other) {
        release();
        pages_ = sys::exchange(other.pages_, nullptr);
        page_count_ = sys::exchange(other.page_count_, 0);
        data_ = sys::exchange(other.data_, nullptr);
        pa_ = sys::exchange(other.pa_, 0);
        size_ = sys::exchange(other.size_, 0);
    }
    return *this;
}

DmaBuffer::~DmaBuffer() {
    release();
}

void DmaBuffer::release() {
    if (pages_)
        pmm::free_pages(pages_, page_count_);
    pages_ = nullptr;
    page_count_ = 0;
    data_ = nullptr;
    pa_ = 0;
    size_ = 0;
}

Result<DmaBuffer> DmaBuffer::alloc(size_t bytes) {
    ENSURE(bytes != 0, Error::Invalid);
    DmaBuffer buffer;
    buffer.page_count_ = (bytes + PG_SIZE - 1) / PG_SIZE;
    buffer.pages_ = pmm::alloc_pages(buffer.page_count_);
    ENSURE(buffer.pages_, Error::NoMem);
    buffer.data_ = pmm::page_to_kva(buffer.pages_);
    buffer.pa_ = pmm::page_to_phys(buffer.pages_);
    buffer.size_ = bytes;
    memset(buffer.data_, 0, buffer.page_count_ * PG_SIZE);
    dma::writeback_invalidate(buffer.pa_, buffer.page_count_ * PG_SIZE);
    return buffer;
}

void DmaBuffer::sync_for_device(size_t offset, size_t bytes) const {
    if (offset < size_ && bytes != 0)
        dma::writeback_invalidate(pa_ + offset, bytes < size_ - offset ? bytes : size_ - offset);
}

void DmaBuffer::sync_for_cpu(size_t offset, size_t bytes) const {
    sync_for_device(offset, bytes);
}
