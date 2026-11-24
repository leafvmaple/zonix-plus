#pragma once

#include <base/types.h>

// With frame pointers, x29 points at the frame record: the saved caller x29, then x30.
struct FrameRecord {
    uintptr_t caller_frame;
    uintptr_t return_address;
};

// The record of the frame whose frame pointer is fp lies FRAME_RECORD_OFFSET below it.
inline constexpr uintptr_t FRAME_RECORD_OFFSET = 0;
