#pragma once

#include <base/types.h>

// With frame pointers, s0 holds the frame's top; the saved caller s0 and ra sit just below it.
struct FrameRecord {
    uintptr_t caller_frame;
    uintptr_t return_address;
};

// The record of the frame whose frame pointer is fp lies FRAME_RECORD_OFFSET below it.
inline constexpr uintptr_t FRAME_RECORD_OFFSET = 16;
