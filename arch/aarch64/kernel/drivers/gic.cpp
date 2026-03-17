// GICv2 driver for QEMU virt machine.

#include "gic.h"

#include <asm/memlayout.h>

namespace {

constexpr uintptr_t GICD_PHYS = 0x08000000;
constexpr uintptr_t GICC_PHYS = 0x08010000;

volatile uint32_t* gicd(uintptr_t off) {
    return reinterpret_cast<volatile uint32_t*>(KERNEL_BASE + GICD_PHYS + off);
}

volatile uint32_t* gicc(uintptr_t off) {
    return reinterpret_cast<volatile uint32_t*>(KERNEL_BASE + GICC_PHYS + off);
}

// GICD offsets
constexpr uintptr_t GICD_CTLR = 0x000;
constexpr uintptr_t GICD_ISENABLER = 0x100;
constexpr uintptr_t GICD_IPRIORITYR = 0x400;
constexpr uintptr_t GICD_ITARGETSR = 0x800;

// GICC offsets
constexpr uintptr_t GICC_CTLR = 0x000;
constexpr uintptr_t GICC_PMR = 0x004;
constexpr uintptr_t GICC_IAR = 0x00C;
constexpr uintptr_t GICC_EOIR = 0x010;

}  // namespace

namespace gic {

int init() {
    // Enable distributor
    *gicd(GICD_CTLR) = 1;

    // Enable CPU interface, allow all priority levels
    *gicc(GICC_CTLR) = 1;
    *gicc(GICC_PMR) = 0xFF;

    return 0;
}

void enable(uint32_t intid) {
    uint32_t reg = intid / 32;
    uint32_t bit = intid % 32;
    *gicd(GICD_ISENABLER + reg * 4) = (1u << bit);

    // Set priority to 0 (highest)
    auto* prio = gicd(GICD_IPRIORITYR + (intid / 4) * 4);
    uint32_t shift = (intid % 4) * 8;
    *prio = (*prio & ~(0xFFu << shift));

    // Route SPIs (IntID >= 32) to CPU 0
    if (intid >= 32) {
        auto* tgt = gicd(GICD_ITARGETSR + (intid / 4) * 4);
        *tgt = (*tgt & ~(0xFFu << shift)) | (0x01u << shift);
    }
}

void send_eoi(uint32_t iar) {
    *gicc(GICC_EOIR) = iar;
}

uint32_t ack() {
    return *gicc(GICC_IAR);
}

}  // namespace gic
