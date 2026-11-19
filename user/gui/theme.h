#pragma once

// Colours shared by the Desktop compositor and the window clients, as XRGB8888.
// This header has no engine dependency so the basic decorations can use it too.
namespace zonix_gui::theme {

inline constexpr unsigned int DESKTOP_TOP = 0x34415E;
inline constexpr unsigned int DESKTOP_BOTTOM = 0x14161D;

inline constexpr unsigned int WINDOW_BACKGROUND = 0x1E1E20;
// A hairline around every window. Focus shows in the shadow and the title.
inline constexpr unsigned int WINDOW_BORDER = 0x404043;
inline constexpr unsigned int WINDOW_BORDER_FOCUSED = 0x55555A;
// Title bars share the colour of toolbars, so the two read as one bar.
inline constexpr unsigned int TITLE_BACKGROUND = 0x2C2C2F;
// Unfocused title bars are blended towards this colour.
inline constexpr unsigned int TITLE_INACTIVE = 0x232325;

inline constexpr unsigned int TEXT = 0xE8E8EA;
inline constexpr unsigned int ACCENT = 0x0A84FF;
inline constexpr unsigned int CLOSE = 0xFF5F57;
inline constexpr unsigned int CURSOR_OUTLINE = 0x0B0B0C;

}  // namespace zonix_gui::theme
