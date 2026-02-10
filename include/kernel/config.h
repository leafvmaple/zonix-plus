// Zonix OS Kernel Configuration
//
// Uncomment/comment these defines to enable/disable kernel modules.
// This is similar to Linux's Kconfig but simplified for a small OS.
//
// After changing this file, run: make clean && make

#pragma once

// ==========================================================================
// Console Drivers
// ==========================================================================

// CGA text mode (0xB8000). Works with BIOS VGA.
// Disable when running UEFI-only (no VGA hardware).
#define CONFIG_CGA          1

// Framebuffer console (GOP/VESA). Pixel-based text rendering.
// Required for UEFI graphical output.
#define CONFIG_FBCONS       1

// Serial console output (COM1, 0x3F8, 115200 baud).
#define CONFIG_SERIAL       1

// Bochs debug port (port 0xE9) output.
#define CONFIG_BOCHS_DBG    1

// ==========================================================================
// Input
// ==========================================================================

// PS/2 keyboard via i8042 controller.
#define CONFIG_PS2KBD       1

// ==========================================================================
// Storage
// ==========================================================================

// Legacy IDE (PIO mode) block device driver.
#define CONFIG_IDE          1

// AHCI (SATA) controller driver.
#define CONFIG_AHCI         1

// ==========================================================================
// Filesystems
// ==========================================================================

// FAT12/16/32 filesystem support.
#define CONFIG_FAT          1

// ==========================================================================
// Memory Management
// ==========================================================================

// Swap to disk support.
#define CONFIG_SWAP         1
