#pragma once

#ifndef __ASSEMBLY__

#include "io.h"
#include "cpu.h"

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

static inline void arch_load_page_table_root(uintptr_t root_pa) {
    lcr3(root_pa);
}

static inline uintptr_t arch_fault_address(void) {
    return rcr2();
}

static inline uintptr_t arch_read_page_table_root(void) {
    return rcr3();
}

static inline void arch_invalidate_tlb_page(void* va) {
    invlpg(va);
}

static inline void arch_invalidate_tlb_range(uintptr_t va, size_t byte_count) {
    for (size_t offset_bytes = 0; offset_bytes < byte_count; offset_bytes += 4096)
        invlpg(reinterpret_cast<void*>(va + offset_bytes));
}

static inline uint8_t arch_port_read8(uint16_t port) {
    return inb(port);
}

static inline uint16_t arch_port_read16(uint16_t port) {
    return inw(port);
}

static inline uint32_t arch_port_read32(uint16_t port) {
    return inl(port);
}

static inline void arch_port_read16_buffer(uint32_t port, void* addr, int element_count) {
    insw(port, addr, element_count);
}

static inline void arch_port_read32_buffer(uint32_t port, void* addr, int element_count) {
    insl(port, addr, element_count);
}

static inline void arch_port_write8(uint16_t port, uint8_t data) {
    outb(port, data);
}

static inline void arch_port_write16(uint16_t port, uint16_t data) {
    outw(port, data);
}

static inline void arch_port_write32(uint16_t port, uint32_t data) {
    outl(port, data);
}

static inline void arch_port_write16_buffer(uint32_t port, const void* addr, int element_count) {
    outsw(port, addr, element_count);
}

static inline void arch_io_wait(void) {
    io_wait();
}

static inline void arch_mb(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void arch_wmb(void) {
    __asm__ volatile("sfence" ::: "memory");
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

using InitStepFn = int (*)();

struct InitStep {
    const char* name;
    InitStepFn fn;
    bool required;
};

const InitStep* arch_early_steps(size_t* count);
const InitStep* arch_pci_steps(size_t* count);
void arch_set_kernel_stack(uintptr_t kernel_stack_top_va);
void arch_irq_eoi(int irq);
void arch_irq_enable_line(int irq);                     /* enable IRQ line in interrupt controller */
int arch_pci_intx_to_irq(uint8_t dev, uint8_t int_pin); /* PCI INTx → platform IRQ */
void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry_va, uintptr_t fn, uintptr_t arg);
void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t stack_va);
void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry_va, uintptr_t user_stack_va);

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
