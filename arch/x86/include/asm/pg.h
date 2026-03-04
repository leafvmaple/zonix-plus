#pragma once

#define PTE_P 0x001      // Present
#define PTE_W 0x002      // Writeable
#define PTE_U 0x004      // User
#define PTE_PWT 0x008    // Write-Through (disable write-back cache for this page)
#define PTE_PCD 0x010    // Cache Disable (disable caching for this page, important for MMIO)
#define PTE_PS  0x080    // Page Size (2MB page when set in PDE)
#define PTE_NX  (1ULL << 63)  // No-Execute (requires EFER.NXE)

#define PTE_USER (PTE_U | PTE_W | PTE_P)

#define PG_SIZE 4096
#define PG_SHIFT 12  // 2^12 = 4096