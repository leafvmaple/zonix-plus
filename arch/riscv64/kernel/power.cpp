#include <kernel/power.h>
#include <asm/arch.h>

#include "drivers/intr.h"

namespace {

constexpr uint64_t SBI_EID_SYSTEM_RESET = 0x53525354;
constexpr uint64_t SBI_FID_SYSTEM_RESET = 0;
constexpr uint64_t SBI_RESET_SHUTDOWN = 0;
constexpr uint64_t SBI_RESET_REASON_NONE = 0;
constexpr int64_t SBI_NOT_SUPPORTED = -2;
constexpr int64_t SBI_INVALID_PARAM = -3;

}  // namespace

Error power::shutdown() {
    intr::Guard guard;
    register uint64_t error __asm__("a0") = SBI_RESET_SHUTDOWN;
    register uint64_t reason __asm__("a1") = SBI_RESET_REASON_NONE;
    register uint64_t function_id __asm__("a6") = SBI_FID_SYSTEM_RESET;
    register uint64_t extension_id __asm__("a7") = SBI_EID_SYSTEM_RESET;
    __asm__ volatile("ecall" : "+r"(error), "+r"(reason) : "r"(function_id), "r"(extension_id) : "memory");
    if (error == 0)
        arch_halt_forever();
    const int64_t status = static_cast<int64_t>(error);
    if (status == SBI_NOT_SUPPORTED)
        return Error::NotSupported;
    return status == SBI_INVALID_PARAM ? Error::Invalid : Error::Fail;
}
