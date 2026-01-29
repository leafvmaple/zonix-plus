#include "e820.h"

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

int e820map_get_items(int index, uint64_t* addr, uint64_t* size, uint32_t* type) {
    struct e820map *e820map = (struct e820map*)(E820_MEM_BASE + KERNEL_BASE);
    if (index >= e820map->nr_map) {
        return 0;
    }
    *addr = e820map->map[index].addr;
    *size = e820map->map[index].size;
    *type = e820map->map[index].type;

    return 1;
}