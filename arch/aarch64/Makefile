# ==========================================================================
# arch/aarch64/Makefile — AArch64 toolchain, flags, and build configuration
#
# Included from the top-level Makefile.
# ==========================================================================

CC      := $(LLVM_BIN)clang --target=aarch64-none-elf
CXX     := $(LLVM_BIN)clang++ --target=aarch64-none-elf
LD      := $(LLD_BIN)ld.lld
# Shared user-space runtime ABI, independent of the application compiler.
USER_ARCH_CFLAGS := -mgeneral-regs-only
# Instruction-generation flags for the TEST=1 user FP/SIMD register fixtures.
USER_TEST_CFLAGS := -g -O2 -nostdinc -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -Iinclude
USER_REGISTER_FIXTURES := fptest fppeer
# The optional Desktop suite's mini-cocos target processor.
GUI_PROCESSOR := aarch64
# Context switches save the user's FP/SIMD state, but traps do not, so the
# kernel must not use those registers, including for copies the optimizer
# vectorizes.
CFLAGS  := -g -Wall $(KERNEL_OPT) -mgeneral-regs-only -nostdinc -fno-builtin -fno-stack-protector \
           -fno-pic -ffreestanding -gdwarf-2
CXXFLAGS := -g -Wall $(KERNEL_OPT) -mgeneral-regs-only -nostdinc -nostdinc++ \
           -fno-builtin -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
           -fno-use-cxa-atexit -fno-threadsafe-statics \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -ffreestanding -std=$(CXX_STANDARD) -gdwarf-2
LDFLAGS := -m aarch64elf -nostdlib

QEMU    := qemu-system-aarch64

# ---------- kernel source directories ----------
KSRCDIR := arch/aarch64/kernel arch/aarch64/kernel/drivers

# ---------- embedded console font (PSF -> ELF .rodata) ----------
FONT_PSF := fonts/console.psf
FONT_OBJ := $(OBJDIR)/fonts/console.psf.o

$(FONT_OBJ): $(FONT_PSF) | $$(dir $$@)
	$(Q)$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		$< $@

ALLOBJS += $(FONT_OBJ)

# ---------- kernel link configuration ----------
KERNEL_EXTRA_OBJS := $(FONT_OBJ)
KERNEL_LD_SCRIPT  := $(SCRIPTDIR)/kernel-aarch64.ld
KERNEL_POST_LINK   = $(Q)$(OBJCOPY) --set-start=0x40080000 $@

# ---------- top-level "all" prerequisites ----------
ALL_PREREQS := $(BINDIR)/kernel $(BINDIR)/BOOTAA64.EFI

# ---------- UEFI firmware ----------
AAVMF ?= $(shell bash $(SCRIPTDIR)/qemu_firmware.sh aarch64)

# ---------- UEFI disk image ----------
$(BINDIR)/zonix-uefi.img: $(BINDIR)/BOOTAA64.EFI $(BINDIR)/kernel $(SCRIPTDIR)/create_uefi_image.sh | $$(dir $$@)
	@echo "  IMG     $@"
	$(Q)BINDIR=$(BINDIR) ARCH=aarch64 bash $(SCRIPTDIR)/create_uefi_image.sh

# ---------- Boot-disk mirror for TEST=1 runners (normal boot uses rootfs.img) ----------
$(BINDIR)/sdcard.img: $(BINDIR)/zonix-uefi.img
	@echo "  IMG     $@"
	$(Q)cp $< $@

# ---------- QEMU run / debug targets ----------
ifneq ($(USER_DISK),)
QEMU_APPS := -drive file=$(USER_DISK),format=raw,if=none,id=apps \
             -device sdhci-pci -device sd-card,drive=apps
endif

.PHONY: qemu debug

qemu: $(BINDIR)/zonix-uefi.img $(BINDIR)/rootfs.img $(USER_DISK)
	$(QEMU) -cpu cortex-a72 \
		-bios $(AAVMF) \
		-readconfig qemu-uefi-aarch64.cfg \
		-serial stdio $(QEMU_APPS) $(QEMU_FLAGS) -no-reboot

debug: $(BINDIR)/zonix-uefi.img $(BINDIR)/rootfs.img $(USER_DISK)
	$(QEMU) -cpu cortex-a72 \
		-bios $(AAVMF) \
		-readconfig qemu-uefi-aarch64.cfg \
		-serial stdio $(QEMU_APPS) $(QEMU_FLAGS) -S -s -no-reboot &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit
