#pragma once

#ifndef __ASSEMBLY__

#include "io.h"
#include "cpu.h"

// Forward declarations for types defined in other arch headers
struct TrapFrame;

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

static inline void arch_load_cr3(uintptr_t cr3) {
    lcr3(cr3);
}

static inline uintptr_t arch_read_cr2(void) {
    return rcr2();
}

static inline uintptr_t arch_read_cr3(void) {
    return rcr3();
}

static inline void arch_invlpg(void* addr) {
    invlpg(addr);
}

static inline uint8_t arch_port_inb(uint16_t port) {
    return inb(port);
}

static inline uint16_t arch_port_inw(uint16_t port) {
    return inw(port);
}

static inline uint32_t arch_port_inl(uint16_t port) {
    return inl(port);
}

static inline void arch_port_insw(uint32_t port, void* addr, int cnt) {
    insw(port, addr, cnt);
}

static inline void arch_port_insl(uint32_t port, void* addr, int cnt) {
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

static inline void arch_port_outsw(uint32_t port, const void* addr, int cnt) {
    outsw(port, addr, cnt);
}

static inline void arch_io_wait(void) {
    io_wait();
}

static inline void arch_spin_hint(void) {
    __asm__ volatile("pause");
}

static inline bool arch_irq_is_enabled(void) {
    return (read_eflags() & FL_IF) != 0;
}

static inline void arch_idle(void) {
    __asm__ volatile("sti; hlt");
}

[[noreturn]] static inline void arch_halt(void) {
    while (true)
        __asm__ volatile("hlt");
}

static inline uintptr_t arch_fault_addr(void) {
    return rcr2();
}

using InitStepFn = int (*)();

struct InitStep {
    const char* name;
    InitStepFn fn;
    bool required;
};

const InitStep* arch_early_steps(size_t* count);
const InitStep* arch_pci_steps(size_t* count);
void arch_switch_rsp0(uintptr_t rsp0);
void arch_irq_eoi(int irq);
void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg);
void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t esp);
void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp);

[[noreturn]] static inline void arch_halt_forever(void) {
    while (true)
        __asm__ volatile("cli; hlt");
}

static inline void* arch_memset(void* s, int c, size_t n) {
    auto* p = static_cast<char*>(s);
    while (n > 0 && (reinterpret_cast<uintptr_t>(p) & 7)) {
        *p++ = static_cast<char>(c);
        n--;
    }
    if (n >= 8) {
        uint64_t val = static_cast<uint8_t>(c);
        val |= val << 8;
        val |= val << 16;
        val |= val << 32;
        size_t qwords = n / 8;
        __asm__ volatile("rep stosq" : "+D"(p), "+c"(qwords) : "a"(val) : "memory");
        n &= 7;
    }
    while (n-- > 0) {
        *p++ = static_cast<char>(c);
    }
    return s;
}

static inline void* arch_memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<char*>(dst);
    auto const* s = static_cast<const char*>(src);
    size_t qwords = n / 8;
    if (qwords > 0) {
        __asm__ volatile("rep movsq" : "+D"(d), "+S"(s), "+c"(qwords)::"memory");
    }
    n &= 7;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

#endif /* !__ASSEMBLY__ */
