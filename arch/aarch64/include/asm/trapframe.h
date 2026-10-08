#pragma once

#define ARM64_TRAP_VECTOR_SYNC 0
#define ARM64_TRAP_VECTOR_IRQ 1
#define ARM64_TRAP_VECTOR_FIQ 2
#define ARM64_TRAP_VECTOR_SERROR 3
#define ARM64_TRAP_VECTOR_OFFSET 0x120
#define ARM64_TRAP_FRAME_SIZE 0x130

#ifndef __ASSEMBLY__

#include <base/types.h>
#include <asm/cpu.h>

struct TrapFrame {
    /* General-purpose registers x0-x30 */
    uint64_t regs[31]{};

    uint64_t sp{};     /* saved SP_EL0 (user stack pointer)   */
    uint64_t pc{};     /* saved ELR_EL1 (return address)      */
    uint64_t pstate{}; /* saved SPSR_EL1 (processor state)    */
    uint64_t esr{};    /* ESR_EL1  (exception syndrome)       */
    uint64_t far{};    /* FAR_EL1  (fault address register)   */

    [[nodiscard]] bool from_user() const { return (pstate & PSTATE_MODE_MASK) == PSTATE_EL0T; }

    uint64_t vector{ARM64_TRAP_VECTOR_SYNC};
    uint64_t padding{};  // Keep the exception stack aligned to 16 bytes.

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
    [[nodiscard]] uint64_t syscall_return() const { return regs[0]; }
    void set_syscall(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
        regs[8] = nr;
        regs[0] = arg0;
        regs[1] = arg1;
        regs[2] = arg2;
    }
};

static_assert(sizeof(TrapFrame) == ARM64_TRAP_FRAME_SIZE);
static_assert(__builtin_offsetof(TrapFrame, vector) == ARM64_TRAP_VECTOR_OFFSET);

extern "C" void trap_dispatch(TrapFrame* tf);
extern "C" void trapret(void);

#endif /* !__ASSEMBLY__ */
