#pragma once

/**
 * @file i8042.h
 * @brief AArch64 stub for x86 i8042 keyboard controller definitions.
 *
 * AArch64 uses UART or virtio for input instead of PS/2.
 * This header provides the keycodes referenced by shared kernel code.
 */

#include <base/types.h>

inline constexpr uint8_t NO = 0;

inline constexpr uint8_t KEY_HOME = 0xE0;
inline constexpr uint8_t KEY_END  = 0xE1;
inline constexpr uint8_t KEY_UP   = 0xE2;
inline constexpr uint8_t KEY_DN   = 0xE3;
inline constexpr uint8_t KEY_LF   = 0xE4;
inline constexpr uint8_t KEY_RT   = 0xE5;
inline constexpr uint8_t KEY_PGUP = 0xE6;
inline constexpr uint8_t KEY_PGDN = 0xE7;
inline constexpr uint8_t KEY_INS  = 0xE8;
inline constexpr uint8_t KEY_DEL  = 0xE9;

inline constexpr char KP_ENTER = '\n';
inline constexpr char KP_DIV   = '/';
