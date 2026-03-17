#pragma once

#ifndef __ASSEMBLY__

/**
 * @file trapframe.h
 * @brief Architecture-specific trap/interrupt frame layout — AArch64 stub.
 *
 * AArch64 exception entry saves registers via kernel vector table code.
 * The layout here matches what the exception entry stub will push.
 *
 * TODO: implement when exception vector table is written.
 */

#include <base/types.h>

/**
 * AArch64 TrapFrame — saved on exception/interrupt entry.
 *
 * Layout must match the push sequence in the exception vector stub.
 */
struct TrapFrame {
    /* General-purpose registers x0-x30 */
    uint64_t regs[31]{};

    /* Special registers */
    uint64_t sp{};     /* saved SP_EL0 (user stack pointer)   */
    uint64_t pc{};     /* saved ELR_EL1 (return address)      */
    uint64_t pstate{}; /* saved SPSR_EL1 (processor state)    */
    uint64_t esr{};    /* ESR_EL1  (exception syndrome)       */
    uint64_t far{};    /* FAR_EL1  (fault address register)   */

    void print() const;
    void print_pgfault() const;

    // ---- Portable accessors for generic kernel code ----

    // Syscall number (AArch64: x8)
    [[nodiscard]] uint64_t syscall_nr() const { return regs[8]; }

    // Syscall arguments (AArch64: x0-x5)
    [[nodiscard]] uint64_t syscall_arg(int n) const {
        if (n >= 0 && n <= 5) {
            return regs[n];
        }
        return 0;
    }

    // Set return value from syscall (AArch64: x0)
    void set_return(uint64_t val) { regs[0] = val; }
};

// Trap handling functions
extern "C" void trap(TrapFrame* tf);
extern "C" void trapret(void);

#endif /* !__ASSEMBLY__ */
