#pragma once
#include <base/types.h>

/*
 * x86_64 TrapRegisters - saved by pushq in trapentry.S
 * Order must match the push/pop sequence in _trap_entry.
 */
struct TrapRegisters {
    uint64_t r15{};
    uint64_t r14{};
    uint64_t r13{};
    uint64_t r12{};
    uint64_t r11{};
    uint64_t r10{};
    uint64_t r9{};
    uint64_t r8{};
    uint64_t rdi{};
    uint64_t rsi{};
    uint64_t rbp{};
    uint64_t rbx{};
    uint64_t rdx{};
    uint64_t rcx{};
    uint64_t rax{};

    void print() const;
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
    TrapRegisters regs{};

    uint64_t trapno{};
    uint64_t err{};

    // Pushed by hardware
    uint64_t rip{};
    uint64_t cs{};
    uint64_t rflags{};
    uint64_t rsp{};    // always present in 64-bit mode
    uint64_t ss{};     // always present in 64-bit mode

    void print() const;
    void print_pgfault() const;
};

// Trap handling functions
extern "C" void trap(TrapFrame *tf);
extern "C" void trapret(void);  // Assembly function to return from trap
