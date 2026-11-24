#pragma once

#include <base/types.h>

// With frame pointers, rbp points at the saved caller rbp, followed by the return address.
struct FrameRecord {
    uintptr_t caller_frame;
    uintptr_t return_address;
};

// The record of the frame whose frame pointer is fp lies FRAME_RECORD_OFFSET below it.
inline constexpr uintptr_t FRAME_RECORD_OFFSET = 0;
