# ==========================================================================
# arch/x86/boot/Makefile — Boot build rules (BIOS + UEFI)
#
# Included from the top-level Makefile.  Dispatches to per-firmware
# sub-Makefiles, each with its own toolchain:
#   bios/ — clang -m32  (16/32-bit C / ASM, freestanding)
#   uefi/ — clang + lld-link  (PE32+ cross-compilation)
# ==========================================================================

BOOT_INC := -I include -I arch/$(ARCH)/include

include arch/$(ARCH)/boot/bios/Makefile
include arch/$(ARCH)/boot/uefi/Makefile

