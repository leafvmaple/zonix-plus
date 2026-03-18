#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

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

struct TrapFrame {
    TrapRegisters regs{};

    uint64_t trapno{};
    uint64_t err{};
    uint64_t rip{};
    uint64_t cs{};
    uint64_t rflags{};
    uint64_t rsp{};  // always present in 64-bit mode
    uint64_t ss{};   // always present in 64-bit mode

    void print() const;
    void print_pgfault() const;

    [[nodiscard]] uint64_t syscall_nr() const { return regs.rax; }
    [[nodiscard]] uint64_t syscall_arg(int n) const {
        switch (n) {
            case 0: return regs.rdi;
            case 1: return regs.rsi;
            case 2: return regs.rdx;
            case 3: return regs.r10;
            case 4: return regs.r8;
            case 5: return regs.r9;
            default: return 0;
        }
    }

    void set_return(uint64_t val) { regs.rax = val; }
};

// Trap handling functions
extern "C" void trap_dispatch(TrapFrame* tf);
extern "C" void trapret(void);

#endif /* !__ASSEMBLY__ */
