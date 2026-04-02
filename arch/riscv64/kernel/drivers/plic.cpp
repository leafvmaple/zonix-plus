/**
 * PLIC driver for RISC-V.
 *
 * Memory map (physical):
 *   0x0C000000 base
 *
 *   Priority register for source N:
 *     base + 4*N          (N = 1..1023)
 *
 *   Pending registers (bit-array, 32 sources/word):
 *     base + 0x1000       (sources 1-31)
 *     base + 0x1004       (sources 32-63)
 *     …
 *
 *   Enable registers for hart H, mode M (context = 2*H + M):
 *     base + 0x2000 + context * 0x80
 *
 *   Priority threshold for context C:
 *     base + 0x200000 + C * 0x1000
 *
 *   Claim / complete register for context C:
 *     base + 0x200004 + C * 0x1000
 */

#include "plic.h"
#include <asm/memlayout.h>
#include <asm/board.h>
#include <asm/arch.h>
#include <base/types.h>

namespace {

constexpr uintptr_t PLIC_PHYS = 0x0C000000UL;
constexpr uintptr_t PLIC_BASE = PLIC_PHYS + KERNEL_BASE;

/* S-mode context (board-specific: hart 0 ctx 1 on QEMU, hart 1 ctx 3 on VF2) */
constexpr int PLIC_CONTEXT = BOARD_PLIC_S_CONTEXT;

constexpr uintptr_t PLIC_PRIORITY = PLIC_BASE;
constexpr uintptr_t PLIC_PENDING = PLIC_BASE + 0x1000;
constexpr uintptr_t PLIC_ENABLE = PLIC_BASE + 0x2000 + PLIC_CONTEXT * 0x80;
constexpr uintptr_t PLIC_THRESHOLD = PLIC_BASE + 0x200000 + PLIC_CONTEXT * 0x1000;
constexpr uintptr_t PLIC_CLAIM = PLIC_BASE + 0x200004 + PLIC_CONTEXT * 0x1000;

/* 32-bit MMIO access helpers */
static volatile uint32_t* mmio32(uintptr_t addr) {
    return reinterpret_cast<volatile uint32_t*>(addr);
}

}  // namespace

namespace plic {

int init() {
    /* Set threshold to 0 so all priorities pass to S-mode hart 0 */
    *mmio32(PLIC_THRESHOLD) = 0;
    arch_mb();
    return 0;
}

void enable(int irq) {
    /* Enable word = irq / 32, bit = irq % 32 */
    uint32_t word = static_cast<uint32_t>(irq) / 32;
    uint32_t bit = static_cast<uint32_t>(irq) % 32;
    volatile uint32_t* en = mmio32(PLIC_ENABLE + word * 4);
    *en |= (1U << bit);
    arch_mb();

    /* Set priority = 1 (just above 0 = disabled) */
    *mmio32(PLIC_PRIORITY + static_cast<uint32_t>(irq) * 4) = 1;
    arch_mb();
}

void set_threshold(uint32_t pri) {
    *mmio32(PLIC_THRESHOLD) = pri;
    arch_mb();
}

uint32_t claim() {
    arch_mb();
    return *mmio32(PLIC_CLAIM);
}

void complete(uint32_t irq) {
    *mmio32(PLIC_CLAIM) = irq;
    arch_mb();
}

}  // namespace plic
