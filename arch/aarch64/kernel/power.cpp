#include <asm/power.h>
#include <asm/arch.h>

#include "drivers/intr.h"

namespace {

constexpr uint64_t PSCI_SYSTEM_OFF = 0x84000008;
constexpr uint64_t PSCI_SYSTEM_RESET = 0x84000009;
constexpr int32_t PSCI_NOT_SUPPORTED = -1;


// QEMU virt without EL2 virtualization exposes PSCI through HVC. SYSTEM_OFF
// and SYSTEM_RESET return only on failure.
Error psci_system_call(uint64_t function) {
    intr::Guard guard;
    register uint64_t function_id __asm__("x0") = function;
    __asm__ volatile("hvc #0" : "+r"(function_id) : : "x1", "x2", "x3", "memory");
    const int32_t status = static_cast<int32_t>(function_id);
    if (status == 0)
        arch_halt_forever();
    return status == PSCI_NOT_SUPPORTED ? Error::NotSupported : Error::Fail;
}

}  // namespace

Error arch_power_off() {
    return psci_system_call(PSCI_SYSTEM_OFF);
}

Error arch_reboot() {
    return psci_system_call(PSCI_SYSTEM_RESET);
}
