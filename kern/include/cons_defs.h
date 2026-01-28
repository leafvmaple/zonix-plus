#pragma once

/**
 * @file console_defs.h
 * @brief Console and terminal related constants
 */

// CGA Display dimensions
#define CRT_ROWS        25
#define CRT_COLS        80

// ASCII character constants
#define ASCII_NUL            0x00
#define ASCII_BACKSPACE      0x08
#define ASCII_TAB            0x09
#define ASCII_NEWLINE        0x0A
#define ASCII_RETURN         0x0D
#define ASCII_PRINTABLE_MIN  0x20
#define ASCII_SPACE          0x20
#define ASCII_PRINTABLE_MAX  0x7F
#define ASCII_DEL            0x7F

// Character attributes for CGA text mode
#define CGA_ATTR_BLACK       0x00
#define CGA_ATTR_WHITE       0x07
#define CGA_ATTR_DEFAULT     0x07

// Convenience definitions
#define SCREEN_ROWS          CRT_ROWS
#define SCREEN_COLS          CRT_COLS
