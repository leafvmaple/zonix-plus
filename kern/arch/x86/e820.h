#pragma once

#include <base/types.h>
#include <arch/x86/segments.h>

#define E820_MAX 20  // number of entries in E820MAP

struct e820map {
    int nr_map;
    struct {
        uint64_t addr;
        uint64_t size;
        uint32_t type;
    } __attribute__((packed)) map[E820_MAX];
};

template<typename F>
void traverse_e820_map(F&& callback) {
    e820map *map = reinterpret_cast<e820map*>(E820_MEM_BASE + KERNEL_BASE);
    for (int i = 0; i < map->nr_map; i++) {
        callback(map->map[i].addr, map->map[i].size, map->map[i].type);
    }
}
