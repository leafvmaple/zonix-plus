#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

inline constexpr uint32_t FL_CF        = 0x00000001;  // Carry Flag
inline constexpr uint32_t FL_PF        = 0x00000004;  // Parity Flag
inline constexpr uint32_t FL_AF        = 0x00000010;  // Auxiliary carry Flag
inline constexpr uint32_t FL_ZF        = 0x00000040;  // Zero Flag
inline constexpr uint32_t FL_SF        = 0x00000080;  // Sign Flag
inline constexpr uint32_t FL_TF        = 0x00000100;  // Trap Flag
inline constexpr uint32_t FL_IF        = 0x00000200;  // Interrupt Flag
inline constexpr uint32_t FL_DF        = 0x00000400;  // Direction Flag
inline constexpr uint32_t FL_OF        = 0x00000800;  // Overflow Flag
inline constexpr uint32_t FL_IOPL_MASK = 0x00003000;  // I/O Privilege Level bitmask
inline constexpr uint32_t FL_IOPL_0    = 0x00000000;  //   IOPL == 0
inline constexpr uint32_t FL_IOPL_1    = 0x00001000;  //   IOPL == 1
inline constexpr uint32_t FL_IOPL_2    = 0x00002000;  //   IOPL == 2
inline constexpr uint32_t FL_IOPL_3    = 0x00003000;  //   IOPL == 3
inline constexpr uint32_t FL_NT        = 0x00004000;  // Nested Task
inline constexpr uint32_t FL_RF        = 0x00010000;  // Resume Flag
inline constexpr uint32_t FL_VM        = 0x00020000;  // Virtual 8086 mode
inline constexpr uint32_t FL_AC        = 0x00040000;  // Alignment Check
inline constexpr uint32_t FL_VIF       = 0x00080000;  // Virtual Interrupt Flag
inline constexpr uint32_t FL_VIP       = 0x00100000;  // Virtual Interrupt Pending
inline constexpr uint32_t FL_ID        = 0x00200000;  // ID flag

static inline void sti(void) __attribute__((always_inline));
static inline void cli(void) __attribute__((always_inline));

static inline void sti(void) {
    asm volatile("sti");
}

static inline void cli(void) {
    asm volatile("cli" ::: "memory");
}

#endif /* !__ASSEMBLY__ */
