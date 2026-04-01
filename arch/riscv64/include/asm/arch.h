#pragma once

/*
 * arch/riscv64/include/asm/arch.h
 *
 * RISC-V S-mode arch abstraction layer.
 * All inline functions here map directly to CSR instructions or
 * no-ops for features that don't exist on RISC-V (I/O ports, etc.)
 */

#ifndef __ASSEMBLY__

#include <base/types.h>
#include <asm/cpu.h>
#include <asm/page.h>

struct TrapFrame;

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */
/* ------------------------------------------------------------------ */

static inline void arch_irq_enable(void) {
    __asm__ volatile("csrsi sstatus, %0" : : "i"(2) : "memory"); /* SIE = bit 1 */
}

static inline void arch_irq_disable(void) {
    __asm__ volatile("csrci sstatus, %0" : : "i"(2) : "memory");
}

/* Returns old sstatus and atomically disables interrupts. */
static inline uint64_t arch_irq_save(void) {
    uint64_t old;
    __asm__ volatile("csrrci %0, sstatus, %1" : "=r"(old) : "i"(2) : "memory");
    return old;
}

/* Restore sstatus from a value previously returned by arch_irq_save(). */
static inline void arch_irq_restore(uint64_t flags) {
    __asm__ volatile("csrw sstatus, %0" : : "r"(flags) : "memory");
}

static inline bool arch_irq_is_enabled(void) {
    uint64_t s;
    __asm__ volatile("csrr %0, sstatus" : "=r"(s));
    return (s & SSTATUS_SIE) != 0;
}

/* ------------------------------------------------------------------ */
/* Page table / TLB                                                    */
/* ------------------------------------------------------------------ */

/*
 * arch_load_cr3 — Write the satp register (Sv39 mode, root page table PA).
 * Caller must pass a value already formatted by MAKE_SATP().
 */
static inline void arch_load_cr3(uintptr_t satp_val) {
    __asm__ volatile("csrw satp, %0\n\t"
                     "sfence.vma"
                     :
                     : "r"(satp_val)
                     : "memory");
}

static inline uintptr_t arch_read_cr3(void) {
    uintptr_t v;
    __asm__ volatile("csrr %0, satp" : "=r"(v));
    return v;
}

/*
 * arch_read_cr2 — Fault address (stval on RISC-V).
 * Valid for page-fault exceptions; may be 0 for other traps.
 */
static inline uintptr_t arch_read_cr2(void) {
    uintptr_t v;
    __asm__ volatile("csrr %0, stval" : "=r"(v));
    return v;
}

static inline uintptr_t arch_fault_addr(void) {
    return arch_read_cr2();
}

/* Shoot a single virtual page from the TLB. */
static inline void arch_invlpg(void* addr) {
    __asm__ volatile("sfence.vma %0, zero" : : "r"(addr) : "memory");
}

/* ------------------------------------------------------------------ */
/* I/O ports — RISC-V has none; all stubs                             */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Memory barriers                                                     */
/* ------------------------------------------------------------------ */

static inline void arch_mb(void) {
    __asm__ volatile("fence rw,rw" ::: "memory");
}

static inline void arch_wmb(void) {
    __asm__ volatile("fence w,w" ::: "memory");
}

static inline void arch_rmb(void) {
    __asm__ volatile("fence r,r" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* CPU hints & power                                                   */
/* ------------------------------------------------------------------ */

static inline void arch_spin_hint(void) {
    __asm__ volatile("nop");
}

static inline void arch_idle(void) {
    __asm__ volatile("wfi");
}

[[noreturn]] static inline void arch_halt(void) {
    while (true) {
        __asm__ volatile("wfi");
    }
}

[[noreturn]] static inline void arch_halt_forever(void) {
    arch_irq_disable();
    while (true) {
        __asm__ volatile("wfi");
    }
}

/* ------------------------------------------------------------------ */
/* Memset / Memcpy (software fallback, no SIMD required)              */
/* ------------------------------------------------------------------ */

static inline void* arch_memset(void* s, int c, size_t n) {
    auto* p = static_cast<uint8_t*>(s);
    while (n-- > 0) {
        *p++ = static_cast<uint8_t>(c);
    }
    return s;
}

static inline void* arch_memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

/* ------------------------------------------------------------------ */
/* Arch init step mechanism                                            */
/* ------------------------------------------------------------------ */

using InitStepFn = int (*)();

struct InitStep {
    const char* name;
    InitStepFn fn;
    bool required;
};

const InitStep* arch_early_steps(size_t* count);
const InitStep* arch_pci_steps(size_t* count);

void arch_switch_rsp0(uintptr_t sp0);

void arch_irq_eoi(int irq);

void arch_irq_enable_line(int irq);
int arch_pci_intx_to_irq(uint8_t dev, uint8_t int_pin);

void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg);
void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t sp);
void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp);

#endif /* !__ASSEMBLY__ */
