#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

struct TrapFrame {
    /* General-purpose registers x0-x30 */
    uint64_t regs[31]{};

    uint64_t sp{};     /* saved SP_EL0 (user stack pointer)   */
    uint64_t pc{};     /* saved ELR_EL1 (return address)      */
    uint64_t pstate{}; /* saved SPSR_EL1 (processor state)    */
    uint64_t esr{};    /* ESR_EL1  (exception syndrome)       */
    uint64_t far{};    /* FAR_EL1  (fault address register)   */

    void print() const;
    void print_pgfault() const;

    [[nodiscard]] uint64_t syscall_nr() const { return regs[8]; }
    [[nodiscard]] uint64_t syscall_arg(int n) const {
        if (n >= 0 && n <= 5) {
            return regs[n];
        }
        return 0;
    }

    void set_return(uint64_t val) { regs[0] = val; }
};

extern "C" void trap_dispatch(TrapFrame* tf);
extern "C" void trapret(void);

#endif /* !__ASSEMBLY__ */
