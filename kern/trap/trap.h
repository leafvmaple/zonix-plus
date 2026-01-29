#pragma once
#include <base/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t reg_edi;
    uint32_t reg_esi;
    uint32_t reg_ebp;
    uint32_t unused; /* Useless */
    uint32_t reg_ebx;
    uint32_t reg_edx;
    uint32_t reg_ecx;
    uint32_t reg_eax;
} trap_regs;

typedef struct {
    trap_regs tf_regs;

    uint32_t tf_trapno;
    uint32_t tf_err;
    uintptr_t tf_eip;
    uint16_t tf_cs;
    uint16_t tf_padding1;
    uint32_t tf_eflags;
    
    // Only present when crossing privilege levels
    uintptr_t tf_esp;
    uint16_t tf_ss;
    uint16_t tf_padding2;
} trap_frame;

// Trap handling functions
void trap(trap_frame *tf);
void trapret(void);  // Assembly function to return from trap

#ifdef __cplusplus
}
#endif
