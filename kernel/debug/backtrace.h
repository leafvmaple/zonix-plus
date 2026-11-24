#pragma once

#include <base/types.h>

namespace debug {

inline constexpr size_t BACKTRACE_DEPTH = 24;

// Follows frame records from the frame pointer fp and stores the return
// addresses found, nearest caller first. Every record must lie inside the
// current task's kernel stack and each caller's frame above the last, so a
// corrupt chain ends the walk instead of faulting. Returns how many were stored.
size_t collect_backtrace(uintptr_t fp, uintptr_t* addresses, size_t capacity);

// Logs pc after the heading, then the return addresses from fp, as error records.
// scripts/symbolize.py turns the addresses into function names.
void log_backtrace(const char* heading, uintptr_t pc, uintptr_t fp);

}  // namespace debug
