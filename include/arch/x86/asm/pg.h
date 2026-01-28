#pragma once

#define PTE_P 0x001      // Present
#define PTE_W 0x002      // Writeable
#define PTE_U 0x004      // User

#define PTE_USER (PTE_U | PTE_W | PTE_P)

#define PG_SIZE 4096
#define PG_SHIFT 12  // 2^12 = 4096