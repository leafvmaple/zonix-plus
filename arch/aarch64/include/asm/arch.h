#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

struct TrapFrame;

static inline void arch_irq_enable(void) {
    __asm__ volatile("msr daifclr, %0" : : "i"(2) : "memory");
}

static inline void arch_irq_disable(void) {
    __asm__ volatile("msr daifset, %0" : : "i"(2) : "memory");
}

static inline uint64_t arch_irq_save(void) {
    uint64_t daif = 0;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    arch_irq_disable();
    return daif;
}

static inline void arch_irq_restore(uint64_t flags) {
    __asm__ volatile("msr daif, %0" ::"r"(flags) : "memory");
}

static inline bool arch_irq_is_enabled(void) {
    uint64_t daif = 0;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    return (daif & (1 << 7)) == 0; /* bit 7 = IRQ mask */
}

static inline void arch_load_cr3(uintptr_t ttbr) {
    __asm__ volatile("msr ttbr0_el1, %0; isb" ::"r"(ttbr) : "memory");
}

static inline uintptr_t arch_read_cr3(void) {
    uintptr_t v = 0;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
    return v;
}

static inline uintptr_t arch_read_cr2(void) {
    uintptr_t v = 0;
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

static inline void arch_flush_tlb_range(uintptr_t va, size_t size) {
    (void)va;
    (void)size;
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

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

static inline void arch_mb(void) {
    __asm__ volatile("dmb ish" ::: "memory");
}

static inline void arch_wmb(void) {
    __asm__ volatile("dmb ishst" ::: "memory");
}

static inline void arch_spin_hint(void) {
    __asm__ volatile("yield");
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

static inline uintptr_t arch_fault_addr(void) {
    return arch_read_cr2();
}

using InitStepFn = int (*)();

struct InitStep {
    const char* name;
    InitStepFn fn;
    bool required;
};

const InitStep* arch_early_steps(size_t* count);
const InitStep* arch_pci_steps(size_t* count);

void arch_switch_rsp0(uintptr_t sp0); /* update EL1 stack for current task */
void arch_irq_eoi(int irq);
void arch_irq_enable_line(int irq);                     /* enable IRQ line in interrupt controller */
int arch_pci_intx_to_irq(uint8_t dev, uint8_t int_pin); /* PCI INTx → platform IRQ */
void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg);
void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t sp);
void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp);

static inline void* arch_memset(void* s, int c, size_t n) {
    auto* p = static_cast<uint8_t*>(s);
    while (n-- > 0) {
        *p++ = static_cast<uint8_t>(c);
    }
    return s;
}

static inline void* arch_memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    auto const* s = static_cast<const uint8_t*>(src);
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

#endif /* !__ASSEMBLY__ */
