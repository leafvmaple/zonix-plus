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
#include "drivers/ide.h"
#include "drivers/ahci.h"
#include "drivers/pci.h"
#include "drivers/cga.h"

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

void init() {}
void setmask(uint16_t) {}
void enable(unsigned int) {}
void send_eoi(unsigned int) {}

}  // namespace pic

// ============================================================================
// Block device stubs (no IDE/AHCI/PCI on QEMU virt basic config)
// ============================================================================

// IDE
IdeConfig IdeManager::s_configs[ide::MAX_DEVICES] = {};
IdeDevice IdeManager::s_devices[ide::MAX_DEVICES] = {};
int IdeManager::s_devices_count = 0;

void IdeManager::init() {}
int IdeManager::get_device_count() {
    return 0;
}
IdeDevice* IdeManager::get_device(int) {
    return nullptr;
}
void IdeManager::interrupt_handler(int) {}
void IdeManager::test() {}
void IdeManager::test_interrupt() {}

int IdeDevice::read(uint32_t, void*, size_t) {
    return -1;
}
int IdeDevice::write(uint32_t, const void*, size_t) {
    return -1;
}
void IdeDevice::print_info() {}
void IdeDevice::detect(const IdeConfig*) {}
void IdeDevice::interrupt() {}

// AHCI
AhciDevice AhciManager::s_devices[ahci::MAX_DEVICES] = {};
int AhciManager::s_devices_count = 0;
AhciPortConfig AhciManager::s_port_configs[ahci::MAX_DEVICES] = {};
uintptr_t AhciManager::s_base = 0;

void AhciManager::init() {}
int AhciManager::get_device_count() {
    return 0;
}
AhciDevice* AhciManager::get_device(int) {
    return nullptr;
}
void AhciManager::test() {}

int AhciDevice::read(uint32_t, void*, size_t) {
    return -1;
}
int AhciDevice::write(uint32_t, const void*, size_t) {
    return -1;
}
void AhciDevice::print_info() {}
void AhciDevice::detect(const AhciPortConfig*, uintptr_t) {}
void AhciDevice::setup_memory() {}
void AhciDevice::identify() {}
void AhciDevice::interrupt() {}
int AhciDevice::issue_cmd(uint8_t, uint32_t, uint16_t, bool) {
    return -1;
}
int AhciDevice::wait_cmd_complete(int) {
    return -1;
}

uint32_t AhciManager::pci_find_bar() {
    return 0;
}
void AhciManager::mmio_write32(uintptr_t, uint32_t, uint32_t) {}
uint32_t AhciManager::mmio_read32(uintptr_t, uint32_t) {
    return 0;
}
int AhciManager::wait_port_ready(uintptr_t, int) {
    return -1;
}
int AhciManager::enable_port(uintptr_t) {
    return -1;
}
void AhciManager::interrupt_handler(int) {}

// ============================================================================
// PCI stub (x86 port I/O based — not applicable on aarch64)
// ============================================================================
uint32_t PCILocation::read_config32(uint8_t) {
    return 0xFFFFFFFF;
}
void PCILocation::write_config32(uint8_t, uint32_t) {}
uint32_t PCILocation::read_bar(uint8_t) {
    return 0;
}
void PCILocation::enable_bus_master() {}
bool PCILocation::find_device_by_class(uint8_t, uint8_t, uint8_t, PCILocation*) {
    return false;
}

// ============================================================================
// CGA stub (x86 VGA text mode — not applicable on aarch64)
// ============================================================================
namespace cga {
void init() {}
void putc(int) {}
void scrup() {}
}  // namespace cga
