#pragma once
#include <base/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TrapRegisters {
    uint32_t m_edi{};
    uint32_t m_esi{};
    uint32_t m_ebp{};
    uint32_t _unused{}; /* Useless */
    uint32_t m_ebx{};
    uint32_t m_edx{};
    uint32_t m_ecx{};
    uint32_t m_eax{};

#ifdef __cplusplus
    void print() const;
#endif
};

struct TrapFrame {
    TrapRegisters m_regs{};

    uint32_t m_trapno{};
    uint32_t m_err{};
    uintptr_t m_eip{};
    uint16_t m_cs{};
    uint16_t _padding1{};
    uint32_t m_eflags{};
    
    // Only present when crossing privilege levels
    uintptr_t m_esp{};
    uint16_t m_ss{};
    uint16_t _padding2{};

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
