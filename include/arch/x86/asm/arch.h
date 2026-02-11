#pragma once

/**
 * @file arch.h
 * @brief Architecture-neutral hardware abstraction layer
 *
 * Provides arch_*() wrappers around platform-specific primitives.
 * Kernel code should use these instead of raw x86 intrinsics (inb, outb,
 * cli, sti, lcr3, etc.) so that an ARM (or other) port only needs to
 * supply an alternative <asm/arch.h>.
 *
 * x86_64 implementation — delegates to the existing helpers in io.h / cpu.h.
 */

#include "io.h"
#include "cpu.h"

// ============================================================================
// Interrupt control
// ============================================================================

static inline void arch_irq_enable(void) {
    sti();
}

static inline void arch_irq_disable(void) {
    cli();
}

static inline uint64_t arch_irq_save(void) {
    return read_eflags();
}

static inline void arch_irq_restore(uint64_t flags) {
    write_eflags(flags);
}

// ============================================================================
// Control registers / TLB
// ============================================================================

static inline void arch_load_cr3(uintptr_t cr3) {
    lcr3(cr3);
}

static inline uintptr_t arch_read_cr2(void) {
    return rcr2();
}

static inline uintptr_t arch_read_cr3(void) {
    return rcr3();
}

static inline void arch_invlpg(void *addr) {
    invlpg(addr);
}

// ============================================================================
// Port I/O  (x86-specific concept; on ARM these would become MMIO helpers)
// ============================================================================

static inline uint8_t arch_port_inb(uint16_t port) {
    return inb(port);
}

static inline uint16_t arch_port_inw(uint16_t port) {
    return inw(port);
}

static inline uint32_t arch_port_inl(uint16_t port) {
    return inl(port);
}

static inline void arch_port_insw(uint32_t port, void *addr, int cnt) {
    insw(port, addr, cnt);
}

static inline void arch_port_insl(uint32_t port, void *addr, int cnt) {
    insl(port, addr, cnt);
}

static inline void arch_port_outb(uint16_t port, uint8_t data) {
    outb(port, data);
}

static inline void arch_port_outw(uint16_t port, uint16_t data) {
    outw(port, data);
}

static inline void arch_port_outl(uint16_t port, uint32_t data) {
    outl(port, data);
}

static inline void arch_port_outsw(uint32_t port, const void *addr, int cnt) {
    outsw(port, addr, cnt);
}

static inline void arch_io_wait(void) {
    io_wait();
}
