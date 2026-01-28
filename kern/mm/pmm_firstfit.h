#pragma once

#include "pmm.h"

// First-Fit memory allocation algorithm
// Searches linearly for the first block that fits the requested size
extern const pmm_manager firstfit_pmm_mgr;