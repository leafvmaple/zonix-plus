#pragma once

#ifndef __ASSEMBLY__

/**
 * @file arch.h
 * @brief Architecture-neutral hardware abstraction layer — AArch64 stub.
 *
 * Provides the same arch_*() API surface as the x86 version.
 * Most functions are stubbed out with TODO markers.
 *
 * TODO: implement each function as sub-systems are brought up.
 */

#include <base/types.h>

struct TrapFrame;

// ============================================================================
// Interrupt control
// ============================================================================

static inline void arch_irq_enable(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline void arch_irq_disable(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

static inline uint64_t arch_irq_save(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    arch_irq_disable();
    return daif;
}

static inline void arch_irq_restore(uint64_t flags) {
    __asm__ volatile("msr daif, %0" ::"r"(flags) : "memory");
}

static inline bool arch_irq_is_enabled(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    return (daif & (1 << 7)) == 0; /* bit 7 = IRQ mask */
}

// ============================================================================
// MMU / TLB (TODO: implement)
// ============================================================================

static inline void arch_load_cr3(uintptr_t ttbr) {
    __asm__ volatile("msr ttbr0_el1, %0; isb" ::"r"(ttbr) : "memory");
}

static inline uintptr_t arch_read_cr3(void) {
    uintptr_t v;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
    return v;
}

static inline uintptr_t arch_read_cr2(void) {
    /* AArch64 fault address register */
    uintptr_t v;
    __asm__ volatile("mrs %0, far_el1" : "=r"(v));
    return v;
}

static inline void arch_invlpg(void* addr) {
    __asm__ volatile("dsb ishst\n"
                     "tlbi vale1is, %0\n"
                     "dsb ish\n"
                     "isb" ::"r"(reinterpret_cast<uintptr_t>(addr) >> 12)
                     : "memory");
}

// ============================================================================
// Port I/O — not applicable on AArch64 (use MMIO instead)
// Stubs are provided so that shared headers compile.
// ============================================================================

static inline uint8_t arch_port_inb(uint16_t) {
    return 0;
}
static inline uint16_t arch_port_inw(uint16_t) {
    return 0;
}
static inline uint32_t arch_port_inl(uint16_t) {
    return 0;
}
static inline void arch_port_insw(uint32_t, void*, int) {}
static inline void arch_port_insl(uint32_t, void*, int) {}
static inline void arch_port_outb(uint16_t, uint8_t) {}
static inline void arch_port_outw(uint16_t, uint16_t) {}
static inline void arch_port_outl(uint16_t, uint32_t) {}
static inline void arch_port_outsw(uint32_t, const void*, int) {}
static inline void arch_io_wait(void) {}

static inline void arch_spin_hint(void) {
    __asm__ volatile("yield");
}

// ============================================================================
// CPU control
// ============================================================================

static inline void arch_idle(void) {
    __asm__ volatile("wfi");
}

[[noreturn]] static inline void arch_halt(void) {
    while (true)
        __asm__ volatile("wfi");
}

[[noreturn]] static inline void arch_halt_forever(void) {
    arch_irq_disable();
    while (true)
        __asm__ volatile("wfi");
}

// ============================================================================
// Fault information
// ============================================================================

static inline uintptr_t arch_fault_addr(void) {
    return arch_read_cr2();
}

// ============================================================================
// Non-inline arch functions (defined in arch_init.cpp)
// ============================================================================

using ArchEarlyStepFn = int (*)();

struct ArchEarlyStep {
    const char* name;
    ArchEarlyStepFn fn;
    bool required;
};

const ArchEarlyStep* arch_early_steps(size_t* count);
void arch_switch_rsp0(uintptr_t sp0); /* update EL1 stack for current task */
void arch_irq_eoi(int irq);
void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg);
void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t sp);
void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp);

// ============================================================================
// Memory operations (generic C fallback; can be replaced with NEON later)
// ============================================================================

static inline void* arch_memset(void* s, int c, size_t n) {
    auto* p = static_cast<uint8_t*>(s);
    while (n-- > 0)
        *p++ = static_cast<uint8_t>(c);
    return s;
}

static inline void* arch_memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    auto const* s = static_cast<const uint8_t*>(src);
    while (n-- > 0)
        *d++ = *s++;
    return dst;
}

#endif /* !__ASSEMBLY__ */
