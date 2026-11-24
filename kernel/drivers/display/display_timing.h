#pragma once

#include <base/types.h>

// A video mode as display controllers and transmitters program it: active,
// sync and total lengths in pixels and lines, counted from the first active
// pixel. CEA-861 modes also carry their video identification code.
struct DisplayTiming {
    uint32_t pixel_khz;
    uint16_t hactive;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t vactive;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    bool hsync_positive;
    bool vsync_positive;
    uint8_t vic;
};

inline constexpr DisplayTiming CEA_1080P60{148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, true, true, 16};
inline constexpr DisplayTiming CEA_720P60{74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, true, true, 4};
