// GICv2 driver for QEMU virt machine.

#include "gic.h"
#include "drivers/mmio.h"

#include <asm/memlayout.h>

namespace {

constexpr uintptr_t GICD_PHYS = 0x08000000;
constexpr uintptr_t GICC_PHYS = 0x08010000;
constexpr uintptr_t GICD_BASE = KERNEL_BASE + GICD_PHYS;
constexpr uintptr_t GICC_BASE = KERNEL_BASE + GICC_PHYS;

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
    mmio::write32(GICD_BASE, GICD_CTLR, 1);

    // Enable CPU interface, allow all priority levels
    mmio::write32(GICC_BASE, GICC_CTLR, 1);
    mmio::write32(GICC_BASE, GICC_PMR, 0xFF);

    return 0;
}

void enable(uint32_t intid) {
    uint32_t reg = intid / 32;
    uint32_t bit = intid % 32;
    mmio::write32(GICD_BASE, GICD_ISENABLER + reg * 4, (1u << bit));

    // Set priority to 0 (highest)
    uint32_t shift = (intid % 4) * 8;
    uintptr_t prio_off = GICD_IPRIORITYR + (intid / 4) * 4;
    uint32_t prio = mmio::read32(GICD_BASE, prio_off);
    prio &= ~(0xFFu << shift);
    mmio::write32(GICD_BASE, prio_off, prio);

    // Route SPIs (IntID >= 32) to CPU 0
    if (intid >= 32) {
        uintptr_t tgt_off = GICD_ITARGETSR + (intid / 4) * 4;
        uint32_t tgt = mmio::read32(GICD_BASE, tgt_off);
        tgt = (tgt & ~(0xFFu << shift)) | (0x01u << shift);
        mmio::write32(GICD_BASE, tgt_off, tgt);
    }
}

void send_eoi(uint32_t iar) {
    mmio::write32(GICC_BASE, GICC_EOIR, iar);
}

uint32_t ack() {
    return mmio::read32(GICC_BASE, GICC_IAR);
}

}  // namespace gic
