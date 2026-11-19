#pragma once

#include <base/types.h>

namespace tss {
// Interrupt Stack Table slot used by the double-fault gate.
inline constexpr uint8_t FAULT_IST = 1;

int init();
void set_rsp0(uintptr_t rsp0);

}  // namespace tss
