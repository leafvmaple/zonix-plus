#pragma once

#include "swap.h"

// FIFO page replacement algorithm
class FifoSwapManager : public SwapManager {
public:
    FifoSwapManager() { name = "fifo"; }

    int init() override;
    int init_mm(MemoryDesc* mm) override;
    int map_swappable(MemoryDesc* mm, uintptr_t addr, Page* page, int swap_in) override;
    int swap_out_victim(MemoryDesc* mm, Page** page_ptr, int in_tick) override;
    int check_swap() override;
};

extern FifoSwapManager swap_mgr_fifo;