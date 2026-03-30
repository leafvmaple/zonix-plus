# ==========================================================================
# arch/aarch64/boot/uefi/Makefile — AArch64 UEFI boot build rules
#
# Toolchain: clang --target=aarch64-pc-windows-msvc → COFF objects
#            lld-link  → PE32+ EFI application
# ==========================================================================

BOOT_UEFI_DIR := arch/$(ARCH)/boot/uefi
BOOT_INC      := -I include -I arch/$(ARCH)/include

UEFI_CXX      := clang++ --target=aarch64-pc-windows-msvc
UEFI_CXXFLAGS := $(BOOT_INC) -ffreestanding -fshort-wchar -nostdinc \
				 -fno-stack-protector -fno-exceptions -fno-rtti \
				 -fno-use-cxa-atexit -fno-threadsafe-statics -std=gnu++17 -O2

# lld-link-18 on Ubuntu 24.04; fall back to lld-link if versioned name missing
UEFI_LD      := $(shell which lld-link-18 2>/dev/null || echo lld-link)
UEFI_LDFLAGS := /subsystem:efi_application /entry:efi_main \
                /nodefaultlib /dll

bootaa64 = $(call totarget,BOOTAA64.EFI)
$(bootaa64): $(BOOT_UEFI_DIR)/bootload.cpp | $$(dir $$@)
	$(Q)mkdir -p $(OBJDIR)/$(BOOT_UEFI_DIR)
	$(Q)$(UEFI_CXX) $(UEFI_CXXFLAGS) -c $< -o $(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.o
	$(Q)$(UEFI_LD) $(UEFI_LDFLAGS) $(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.o /out:$@
	@echo "  UEFI    $@"
