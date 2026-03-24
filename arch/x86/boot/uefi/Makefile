# ==========================================================================
# arch/x86/boot/uefi/Makefile — UEFI boot build rules
#
# Included from arch/x86/boot/Makefile.  Inherits all top-level variables.
# Toolchain: clang --target=x86_64-pc-windows-msvc -> COFF objects
#            lld-link -> PE32+ EFI application
#
# Uses the project's own <uefi/uefi.h> types — no gnu-efi dependency.
# ==========================================================================

BOOT_UEFI_DIR := arch/$(ARCH)/boot/uefi

UEFI_CC      := clang --target=x86_64-pc-windows-msvc
UEFI_CFLAGS  := $(BOOT_INC) -ffreestanding -fshort-wchar -nostdinc \
                -fno-stack-protector -mno-red-zone -O2

# lld-link-18 on Ubuntu 24.04; fall back to lld-link if versioned name missing
UEFI_LD      := $(shell which lld-link-18 2>/dev/null || echo lld-link)
UEFI_LDFLAGS := /subsystem:efi_application /entry:efi_main \
                /nodefaultlib /dll

bootx64 = $(call totarget,BOOTX64.EFI)
$(bootx64): $(BOOT_UEFI_DIR)/bootload.c | $$(dir $$@)
	$(Q)mkdir -p $(OBJDIR)/$(BOOT_UEFI_DIR)
	$(Q)$(UEFI_CC) $(UEFI_CFLAGS) -c $< -o $(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.o
	$(Q)$(UEFI_LD) $(UEFI_LDFLAGS) $(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.o /out:$@
	@echo "  UEFI    $@"
