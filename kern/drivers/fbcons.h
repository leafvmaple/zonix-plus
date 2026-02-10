#pragma once

#include <kernel/config.h>
#include <base/types.h>

namespace fbcons {

#ifdef CONFIG_FBCONS

// Late initialization: map framebuffer MMIO and activate console (requires VMM)
void late_init();

// Initialize framebuffer console with mapped virtual address
void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp);

// Write a character to framebuffer console
void putc(int c);

// Called from timer interrupt to blink cursor
void tick();

// Check if framebuffer console is active
bool is_active();
#endif

} // namespace fbcons
