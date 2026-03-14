#pragma once

/**
 * @file pg.h
 * @brief AArch64 raw PTE bit definitions and page size constants.
 *
 * Equivalent to x86's pg.h. Provides the low-level hardware PTE bits
 * that mmu.h and page.h build upon.
 */

/* ===================================================================
 * AArch64 native PTE bits (VMSAv8-64, stage-1, 4KB granule)
 * =================================================================== */

#define PTE_VALID (1UL << 0)
#define PTE_TABLE (1UL << 1)  /* table descriptor (levels 0-2) */
#define PTE_BLOCK (0UL << 1)  /* block descriptor (levels 1-2) */
#define PTE_PAGE  (1UL << 1)  /* page descriptor (level 3)     */
#define PTE_AF    (1UL << 10) /* access flag                   */

/* AttrIndex[2:0] in bits [4:2] */
#define PTE_ATTR_NORMAL (0UL << 2) /* use MAIR index 0 (normal)     */
#define PTE_ATTR_DEVICE (1UL << 2) /* use MAIR index 1 (device)     */

/* AP[2:1] in bits [7:6] */
#define PTE_AP_RW_EL1 (0UL << 6) /* EL1 read/write, EL0 none      */
#define PTE_AP_RW_ALL (1UL << 6) /* EL1+EL0 read/write            */
#define PTE_AP_RO_EL1 (2UL << 6) /* EL1 read-only, EL0 none       */
#define PTE_AP_RO_ALL (3UL << 6) /* EL1+EL0 read-only             */

#define PTE_UXN (1UL << 54) /* unprivileged execute-never    */
#define PTE_PXN (1UL << 53) /* privileged execute-never      */

/* ===================================================================
 * Page size
 * =================================================================== */

#define PG_SIZE  4096
#define PG_SHIFT 12
#define PG_MASK  (PG_SIZE - 1)
