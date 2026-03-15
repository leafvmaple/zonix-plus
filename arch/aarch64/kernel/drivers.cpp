/**
 * @file drivers.cpp
 * @brief AArch64 driver stubs for x86-specific hardware.
 *
 * Provides empty implementations for driver interfaces that are
 * referenced by shared kernel code but have no aarch64 equivalent.
 * This avoids #ifdef pollution in shared code.
 */

#include "drivers/kbd.h"
#include "drivers/fbcons.h"
#include "drivers/serial.h"
#include "drivers/pic.h"
#include "drivers/pit.h"

// ============================================================================
// Keyboard stub (PL011 UART handles input in cons.cpp)
// ============================================================================
namespace kbd {

void init() {}

int getc() {
    return -1;
}

void intr() {}

char getc_blocking() {
    // TODO: implement via UART RX interrupt
    while (true) {
        __asm__ volatile("wfi");
    }
}

}  // namespace kbd

// ============================================================================
// Framebuffer console stub (no GOP/VESA on QEMU virt)
// ============================================================================
namespace fbcons {

void late_init() {}
void init(uintptr_t, uint32_t, uint32_t, uint32_t, uint8_t) {}
void putc(int) {}
void tick() {}
bool is_active() {
    return false;
}

}  // namespace fbcons

// ============================================================================
// Serial stub (PL011 already used as main console)
// ============================================================================
namespace serial {

void init() {}
void putc(int) {}

}  // namespace serial

// ============================================================================
// PIC stub (aarch64 uses GIC instead)
// ============================================================================
namespace pic {

int init() {
    return 0;
}
void setmask(uint16_t) {}
void enable(unsigned int) {}
void send_eoi(unsigned int) {}

}  // namespace pic
