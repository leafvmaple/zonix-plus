#pragma once

/**
 * i8042.h — RISC-V stub for x86 PS/2 keyboard controller.
 * RISC-V uses UART (16550) for console input instead of PS/2.
 * Keycodes are provided here so shared kernel code compiles unchanged.
 */

#include <base/types.h>

inline constexpr uint8_t NO = 0;

inline constexpr uint8_t KEY_HOME = 0xE0;
inline constexpr uint8_t KEY_END = 0xE1;
inline constexpr uint8_t KEY_UP = 0xE2;
inline constexpr uint8_t KEY_DN = 0xE3;
inline constexpr uint8_t KEY_LF = 0xE4;
inline constexpr uint8_t KEY_RT = 0xE5;
inline constexpr uint8_t KEY_PGUP = 0xE6;
inline constexpr uint8_t KEY_PGDN = 0xE7;
inline constexpr uint8_t KEY_INS = 0xE8;
inline constexpr uint8_t KEY_DEL = 0xE9;

inline constexpr char KP_ENTER = '\n';
inline constexpr char KP_DIV = '/';
