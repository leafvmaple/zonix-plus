#pragma once

/* Byte offsets into struct Context — used by switch.S and C++ code alike.
 * These must be kept in sync with the field order below. */
#define CTX_RA   0
#define CTX_SP   8
#define CTX_S0   16
#define CTX_S1   24
#define CTX_S2   32
#define CTX_S3   40
#define CTX_S4   48
#define CTX_S5   56
#define CTX_S6   64
#define CTX_S7   72
#define CTX_S8   80
#define CTX_S9   88
#define CTX_S10  96
#define CTX_S11  104
#define CTX_SIZE 112

#ifndef __ASSEMBLY__

#include <base/types.h>

/*
 * RISC-V callee-saved registers for context switching.
 *
 * The RISC-V calling convention mandates that the following registers
 * are callee-saved (preserved across function calls):
 *   ra (x1)      — return address
 *   sp (x2)      — stack pointer
 *   s0-s1 (x8-x9)
 *   s2-s11 (x18-x27)
 *
 * switch_to() in switch.S saves/restores exactly these registers.
 * The layout here must match the offsets used in switch.S exactly.
 */
struct Context {
    uint64_t ra{};  /* x1  — return address / entry point          */
    uint64_t sp{};  /* x2  — kernel stack pointer                  */
    uint64_t s0{};  /* x8  — callee-saved (also frame pointer)     */
    uint64_t s1{};  /* x9                                          */
    uint64_t s2{};  /* x18                                         */
    uint64_t s3{};  /* x19                                         */
    uint64_t s4{};  /* x20                                         */
    uint64_t s5{};  /* x21                                         */
    uint64_t s6{};  /* x22                                         */
    uint64_t s7{};  /* x23                                         */
    uint64_t s8{};  /* x24                                         */
    uint64_t s9{};  /* x25                                         */
    uint64_t s10{}; /* x26                                         */
    uint64_t s11{}; /* x27                                         */

    void set_entry(uintptr_t addr) { ra = addr; }
    void set_stack(uintptr_t s) { sp = s; }
};

/* Assembly interface (switch.S) */
extern "C" void forkret(void);
extern "C" void trapret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);

#endif /* !__ASSEMBLY__ */
