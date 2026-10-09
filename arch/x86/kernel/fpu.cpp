#include <asm/cr.h>
#include <asm/fpu.h>

namespace fpu {

int init() {
    constexpr uint32_t required_features = (1u << 24) | (1u << 25) | (1u << 26);
    uint32_t eax = 1;
    uint32_t ebx{}, ecx{}, edx{};
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if ((edx & required_features) != required_features) {
        return -1;
    }

    uintptr_t cr0{}, cr4{};
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~(CR0_EM | CR0_TS)) | CR0_MP | CR0_NE;
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    // Only legacy FP/SSE state is switched. AVX/XSAVE must stay unavailable.
    cr4 = (cr4 & ~CR4_OSXSAVE) | CR4_OSFXSR | CR4_OSXMMEXCPT;
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    FloatingPointState initial;
    initial.restore();
    return 0;
}

}  // namespace fpu
