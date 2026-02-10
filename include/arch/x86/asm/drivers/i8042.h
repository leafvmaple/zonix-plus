#pragma once

// I8042 Keyboard Controller

#define KBD_DATA_REG   0x60  // Keyboard Data
#define KBD_STATUS_REG 0x64  // Keyboard Controller

#define KBD_OBF_FULL 0x01
#define KBD_IBF_FULL 0x02

#define KBD_CMD_WO_PORT 0xD1   // Write to Output Port

#define KBD_A20_ENABLE 0xDF

// Special keycodes
#define NO 0

#define KEY_HOME 0xE0
#define KEY_END 0xE1
#define KEY_UP 0xE2
#define KEY_DN 0xE3
#define KEY_LF 0xE4
#define KEY_RT 0xE5
#define KEY_PGUP 0xE6
#define KEY_PGDN 0xE7
#define KEY_INS 0xE8
#define KEY_DEL 0xE9

#define KP_ENTER '\n'
#define KP_DIV   '/'