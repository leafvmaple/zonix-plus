/**
 * AArch64 minimal kern_init — bare-metal boot test.
 *
 * This is a standalone entry point used during early bringup.
 * It bypasses the full kernel subsystem stack and outputs directly
 * to the PL011 UART to verify that:
 *   - head.S page table setup is correct
 *   - Higher-half jump works
 *   - C++ runtime environment is functional
 *
 * Once driver/subsystem portability improves this will be replaced
 * by the shared kernel/init.cpp.
 */

#include <asm/arch.h>
#include <asm/memlayout.h>
#include <kernel/bootinfo.h>

// PL011 UART (QEMU virt)
static constexpr uintptr_t UART_PHYS = 0x09000000;

static volatile uint32_t* uart() {
    return phys_to_virt<uint32_t>(UART_PHYS);
}

static void uart_putc(char c) {
    if (c == '\n')
        uart()[0] = '\r';
    uart()[0] = static_cast<uint32_t>(c);
}

static void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

static void uart_puthex(uint64_t v) {
    const char* hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xF]);
    }
}

// C++ global constructors
extern "C" {
using ctor_func = void (*)();
extern ctor_func __init_array_start[];
extern ctor_func __init_array_end[];
}

static void cxx_init() {
    for (auto* fn = __init_array_start; fn < __init_array_end; fn++) {
        (*fn)();
    }
}

extern "C" __attribute__((noreturn)) int kern_init(struct boot_info* boot_info) {
    // C++ global constructors
    cxx_init();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Zonix OS (aarch64) Boot Test\n");
    uart_puts("========================================\n");

    uart_puts("kern_init: entered higher-half kernel\n");

    // Verify boot_info
    if (boot_info && boot_info->magic == BOOT_INFO_MAGIC) {
        uart_puts("boot_info: magic OK (0x12345678)\n");
    } else {
        uart_puts("boot_info: INVALID or NULL!\n");
    }

    // Print some addresses to verify mapping
    uart_puts("KERNEL_BASE = ");
    uart_puthex(KERNEL_BASE);
    uart_puts("\n");

    uart_puts("kern_init @ ");
    uart_puthex(reinterpret_cast<uint64_t>(&kern_init));
    uart_puts("\n");

    uart_puts("UART virt  @ ");
    uart_puthex(reinterpret_cast<uint64_t>(uart()));
    uart_puts("\n");

    uart_puts("\nAArch64 boot successful! Halting.\n");

    // Halt
    while (true)
        __asm__ volatile("wfi");
}
