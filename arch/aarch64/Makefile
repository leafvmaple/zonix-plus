# ==========================================================================
# arch/aarch64/Makefile — AArch64 toolchain, flags, and build configuration
#
# Included from the top-level Makefile.
# ==========================================================================

CC      := $(LLVM_BIN)clang --target=aarch64-none-elf
CXX     := $(LLVM_BIN)clang++ --target=aarch64-none-elf
LD      := $(LLD_BIN)ld.lld
CFLAGS  := -g -Wall -O0 -nostdinc -fno-builtin -fno-stack-protector \
           -fno-pic -ffreestanding -gdwarf-2
CXXFLAGS := -g -Wall -O0 -nostdinc -nostdinc++ \
           -fno-builtin -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
           -fno-use-cxa-atexit -fno-threadsafe-statics \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -ffreestanding -std=$(CXX_STANDARD) -gdwarf-2
LDFLAGS := -m aarch64elf -nostdlib

QEMU    := qemu-system-aarch64

# ---------- kernel source directories ----------
KSRCDIR := kernel              \
           arch/aarch64/kernel \
           arch/aarch64/kernel/drivers \
           kernel/debug        \
           kernel/cons         \
           kernel/cons/cmd     \
		   kernel/trap         \
           kernel/sched        \
           kernel/mm           \
           kernel/sync         \
           kernel/block        \
           kernel/fs           \
		   kernel/fs/fat       \
           kernel/exec         \
           kernel/drivers

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

# ---------- SD card image (128 MiB, power-of-2 required by QEMU) ----------
$(BINDIR)/sdcard.img: $(BINDIR)/zonix-uefi.img
	@echo "  IMG     $@"
	$(Q)cp $< $@

# ---------- QEMU run / debug targets ----------
.PHONY: qemu debug

qemu: $(BINDIR)/zonix-uefi.img $(BINDIR)/sdcard.img $(BINDIR)/userdata.img
	$(QEMU) -cpu cortex-a72 \
		-bios $(AAVMF) \
		-readconfig qemu-uefi-aarch64.cfg \
		-serial stdio $(QEMU_FLAGS) -no-reboot

debug: $(BINDIR)/zonix-uefi.img $(BINDIR)/sdcard.img $(BINDIR)/userdata.img
	$(QEMU) -cpu cortex-a72 \
		-bios $(AAVMF) \
		-readconfig qemu-uefi-aarch64.cfg \
		-serial stdio $(QEMU_FLAGS) -S -s -no-reboot &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit
