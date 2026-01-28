#pragma once

#include <base/types.h>

typedef void (*e820_cb)(uint64_t addr, uint64_t size, uint32_t type, void *arg);

int e820map_get_items(int index, uint64_t* addr, uint64_t* size, uint32_t* type);