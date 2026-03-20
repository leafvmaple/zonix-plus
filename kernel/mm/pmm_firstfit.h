#pragma once

#include "pmm.h"

class FirstFitPageAllocator : public PageAllocator {
public:
    FirstFitPageAllocator();

    void init() override;
    void init_memmap(Page* base, size_t n) override;
    Page* alloc(size_t n) override;
    void free(Page* base, size_t n) override;
    size_t nr_free_pages() override;
    void check() override;

private:
    FreeArea free_{};
};