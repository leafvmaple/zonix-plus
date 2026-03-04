#pragma once

/* Control Register flags */

#define CR0_PE 0x00000001  // Protection Enable
#define CR0_MP 0x00000002  // Monitor coProcessor
#define CR0_EM 0x00000004  // Emulation
#define CR0_TS 0x00000008  // Task Switched
#define CR0_ET 0x00000010  // Extension Type
#define CR0_NE 0x00000020  // Numeric Error
#define CR0_WP 0x00010000  // Write Protect
#define CR0_AM 0x00040000  // Alignment Mask
#define CR0_NW 0x20000000  // Not Write through
#define CR0_CD 0x40000000  // Cache Disable
#define CR0_PG 0x80000000  // Paging

/* CR4 flags */
#define CR4_PSE 0x00000010  // Page Size Extension
#define CR4_PAE 0x00000020  // Physical Address Extension
#define CR4_PGE 0x00000080  // Page Global Enable

/* Model Specific Registers (MSR) */
#define MSR_EFER 0xC0000080  // Extended Feature Enable Register
#define EFER_SCE 0x001       // System Call Extensions
#define EFER_LME 0x100       // Long Mode Enable
#define EFER_LMA 0x400       // Long Mode Active
#define EFER_NXE 0x800       // No-Execute Enable