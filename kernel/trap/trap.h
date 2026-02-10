#pragma once
#include <base/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * x86_64 TrapRegisters - saved by pushq in trapentry.S
 * Order must match the push/pop sequence in _trap_entry.
 */
struct TrapRegisters {
    uint64_t m_r15{};
    uint64_t m_r14{};
    uint64_t m_r13{};
    uint64_t m_r12{};
    uint64_t m_r11{};
    uint64_t m_r10{};
    uint64_t m_r9{};
    uint64_t m_r8{};
    uint64_t m_rdi{};
    uint64_t m_rsi{};
    uint64_t m_rbp{};
    uint64_t m_rbx{};
    uint64_t m_rdx{};
    uint64_t m_rcx{};
    uint64_t m_rax{};

#ifdef __cplusplus
    void print() const;
#endif
};

/*
 * x86_64 TrapFrame - pushed by hardware + software
 * Layout (from high address to low):
 *   [hardware push on interrupt]
 *     SS, RSP, RFLAGS, CS, RIP, (error code if any)
 *   [software push in vectors.S]
 *     error_code (or 0), trapno
 *   [software push in trapentry.S]
 *     all general-purpose registers (TrapRegisters)
 */
struct TrapFrame {
    TrapRegisters m_regs{};

    uint64_t m_trapno{};
    uint64_t m_err{};

    // Pushed by hardware
    uint64_t m_rip{};
    uint64_t m_cs{};
    uint64_t m_rflags{};
    uint64_t m_rsp{};    // always present in 64-bit mode
    uint64_t m_ss{};     // always present in 64-bit mode

#ifdef __cplusplus
    void print() const;
    void print_pgfault() const;
#endif
};

// Trap handling functions
void trap(TrapFrame *tf);
void trapret(void);  // Assembly function to return from trap

#ifdef __cplusplus
}
#endif
