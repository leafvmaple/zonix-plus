# ==========================================================================
# arch/x86/boot/uefi/Makefile — UEFI boot build rules
#
# Included from arch/x86/boot/Makefile.  Inherits all top-level variables.
# Toolchain: x86_64-w64-mingw32-gcc (PE32+ cross-compilation)
# ==========================================================================

BOOT_UEFI_DIR := arch/$(ARCH)/boot/uefi

UEFI_CC      := x86_64-w64-mingw32-gcc
UEFI_CFLAGS  := $(BOOT_INC) -I /usr/include/efi -I /usr/include/efi/x86_64 \
                -nostdlib -ffreestanding -fshort-wchar -mno-red-zone -O2
UEFI_LDFLAGS := -Wl,--entry=efi_main -Wl,--subsystem,10 -Wl,--image-base,0 \
                -L/usr/lib -lefi -lgnuefi

bootx64 = $(call totarget,BOOTX64.EFI)
$(bootx64): $(BOOT_UEFI_DIR)/bootload.c | $$(dir $$@)
	$(Q)mkdir -p $(OBJDIR)/$(BOOT_UEFI_DIR)
	$(Q)$(UEFI_CC) $(UEFI_CFLAGS) $< -o $@ $(UEFI_LDFLAGS)
	@echo "  UEFI    $@"
