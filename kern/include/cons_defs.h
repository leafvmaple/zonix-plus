#pragma once

#include <base/types.h>

/**
 * @file console_defs.h
 * @brief Console and terminal related constants
 */

namespace console {

// CGA Display dimensions
inline constexpr int ROWS = 25;
inline constexpr int COLS = 80;

// ASCII character constants
namespace ascii {
inline constexpr char NUL           = 0x00;
inline constexpr char BACKSPACE     = 0x08;
inline constexpr char TAB           = 0x09;
inline constexpr char NEWLINE       = 0x0A;
inline constexpr char RETURN        = 0x0D;
inline constexpr char PRINTABLE_MIN = 0x20;
inline constexpr char SPACE         = 0x20;
inline constexpr char PRINTABLE_MAX = 0x7F;
inline constexpr char DEL           = 0x7F;
} // namespace ascii

// Character attributes for CGA text mode
namespace attr {
inline constexpr uint8_t BLACK   = 0x00;
inline constexpr uint8_t WHITE   = 0x07;
inline constexpr uint8_t DEFAULT = 0x07;
} // namespace attr

} // namespace console

// Legacy compatibility
#define CRT_ROWS          console::ROWS
#define CRT_COLS          console::COLS
#define ASCII_NUL         console::ascii::NUL
#define ASCII_BACKSPACE   console::ascii::BACKSPACE
#define ASCII_TAB         console::ascii::TAB
#define ASCII_NEWLINE     console::ascii::NEWLINE
#define ASCII_RETURN      console::ascii::RETURN
#define ASCII_PRINTABLE_MIN console::ascii::PRINTABLE_MIN
#define ASCII_SPACE       console::ascii::SPACE
#define ASCII_PRINTABLE_MAX console::ascii::PRINTABLE_MAX
#define ASCII_DEL         console::ascii::DEL
#define CGA_ATTR_BLACK    console::attr::BLACK
#define CGA_ATTR_WHITE    console::attr::WHITE
#define CGA_ATTR_DEFAULT  console::attr::DEFAULT
#define SCREEN_ROWS       CRT_ROWS
#define SCREEN_COLS       CRT_COLS
