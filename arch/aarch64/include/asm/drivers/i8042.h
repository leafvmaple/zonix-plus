#pragma once

/**
 * @file i8042.h
 * @brief AArch64 stub for x86 i8042 keyboard controller definitions.
 *
 * AArch64 uses UART or virtio for input instead of PS/2.
 * This header provides the keycodes referenced by shared kernel code.
 */

#define NO 0

#define KEY_HOME 0xE0
#define KEY_END  0xE1
#define KEY_UP   0xE2
#define KEY_DN   0xE3
#define KEY_LF   0xE4
#define KEY_RT   0xE5
#define KEY_PGUP 0xE6
#define KEY_PGDN 0xE7
#define KEY_INS  0xE8
#define KEY_DEL  0xE9

#define KP_ENTER '\n'
#define KP_DIV   '/'
