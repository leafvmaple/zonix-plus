#pragma once

// Colours shared by the Desktop compositor and the window clients, as XRGB8888.
// This header has no engine dependency so the basic decorations can use it too.
namespace zonix_gui::theme {

inline constexpr unsigned int DESKTOP_TOP = 0x1D3144;
inline constexpr unsigned int DESKTOP_BOTTOM = 0x0D1620;

inline constexpr unsigned int WINDOW_BACKGROUND = 0x111D29;
inline constexpr unsigned int WINDOW_BORDER = 0x3A4E60;
inline constexpr unsigned int WINDOW_BORDER_FOCUSED = 0x5FC4C0;
inline constexpr unsigned int TITLE_BACKGROUND = 0x243A4D;
inline constexpr unsigned int TITLE_DIVIDER = 0x16242F;
// Unfocused title bars are blended towards this colour.
inline constexpr unsigned int TITLE_INACTIVE = 0x16232F;

inline constexpr unsigned int TEXT = 0xF1F5F9;
inline constexpr unsigned int ACCENT = 0x5FC4C0;
inline constexpr unsigned int CLOSE = 0xD9606B;
inline constexpr unsigned int CURSOR_OUTLINE = 0x0A1017;

}  // namespace zonix_gui::theme
