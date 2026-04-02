# ==========================================================================
# arch/riscv64/Makefile — RISC-V 64-bit toolchain, flags, and build config
#
# Included from the top-level Makefile.
#
# Target: QEMU virt machine or VisionFive2 board
#   - Boots via UEFI (BOOTRISCV64.EFI)
#   - Sv39 page tables (39-bit VA, kernel at 0xFFFFFFC000000000)
#   - SBI timer, 16550 UART, PLIC interrupt controller
#
# Board selection: make ARCH=riscv64 BOARD=qemu_virt    (default)
#                  make ARCH=riscv64 BOARD=visionfive2
# ==========================================================================

BOARD ?= qemu_virt
BOARD_DEFINE := -DBOARD_$(shell echo $(BOARD) | tr a-z A-Z)

CC      := clang --target=riscv64-none-elf
CXX     := clang++ --target=riscv64-none-elf
LD      := ld.lld
CFLAGS  := -g -Wall -O0 -nostdinc -fno-builtin -fno-stack-protector \
           -fno-pic -ffreestanding -gdwarf-2 -march=rv64gc -mabi=lp64d \
           -mcmodel=medany $(BOARD_DEFINE)
CXXFLAGS := -g -Wall -O0 -nostdinc -nostdinc++ \
           -fno-builtin -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
           -fno-use-cxa-atexit -fno-threadsafe-statics \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -ffreestanding -std=gnu++17 -gdwarf-2 \
           -march=rv64gc -mabi=lp64d -mcmodel=medany $(BOARD_DEFINE)

# Board-specific kernel physical load address for the linker script
ifeq ($(BOARD),visionfive2)
  BOARD_KERNEL_PHYS := 0x40200000
else
  BOARD_KERNEL_PHYS := 0x80200000
endif

LDFLAGS := -m elf64lriscv -nostdlib --defsym KERNEL_PHYS=$(BOARD_KERNEL_PHYS)

QEMU    := qemu-system-riscv64

# ---------- kernel source directories ----------
KSRCDIR := kernel                \
           arch/riscv64/kernel   \
           arch/riscv64/kernel/drivers \
           kernel/debug          \
           kernel/cons           \
           kernel/trap           \
           kernel/sched          \
           kernel/mm             \
           kernel/sync           \
           kernel/block          \
           kernel/fs             \
           kernel/fs/fat         \
           kernel/exec           \
           kernel/drivers

# ---------- embedded console font (PSF -> ELF .rodata via .incbin) ----------
# objcopy creates an ELF without the RISC-V FP-ABI attribute, causing a linker
# error when mixed with -mabi=lp64d objects.  Work around by generating a small
# assembly wrapper that uses .incbin with the correct ABI.
FONT_PSF := fonts/console.psf
FONT_OBJ := $(OBJDIR)/fonts/console.psf.o
FONT_WRAPPER_S := $(OBJDIR)/fonts/console.psf.S

$(FONT_WRAPPER_S): $(FONT_PSF) | $$(dir $$@)
	@echo ".section .rodata,\"a\",@progbits"                     > $@
	@echo ".globl _binary_fonts_console_psf_start"              >> $@
	@echo "_binary_fonts_console_psf_start:"                    >> $@
	@echo '.incbin "$(CURDIR)/$(FONT_PSF)"'                     >> $@
	@echo ".globl _binary_fonts_console_psf_end"                >> $@
	@echo "_binary_fonts_console_psf_end:"                      >> $@

$(FONT_OBJ): $(FONT_WRAPPER_S) | $$(dir $$@)
	$(Q)$(CC) $(CFLAGS) -D__ASSEMBLY__ -c $< -o $@

ALLOBJS += $(FONT_OBJ)

# ---------- kernel link configuration ----------
KERNEL_EXTRA_OBJS := $(FONT_OBJ)
KERNEL_LD_SCRIPT  := $(SCRIPTDIR)/kernel-riscv64.ld

# ---------- top-level "all" prerequisites ----------
ALL_PREREQS := $(BINDIR)/kernel $(BINDIR)/BOOTRISCV64.EFI $(BINDIR)/zonix-uefi.img $(BINDIR)/sdcard.img

# ---------- UEFI disk image ----------
$(BINDIR)/zonix-uefi.img: $(BINDIR)/BOOTRISCV64.EFI $(BINDIR)/kernel | $$(dir $$@)
	@echo "  IMG     $@"
	$(Q)BINDIR=$(BINDIR) ARCH=riscv64 bash $(SCRIPTDIR)/create_uefi_image.sh

# ---------- SD card image (kernel-visible system disk) ----------
$(BINDIR)/sdcard.img: $(BINDIR)/zonix-uefi.img
	@echo "  IMG     $@"
	$(Q)cp $< $@

# ---------- QEMU run / debug targets ----------
AAVMF ?= /usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd

.PHONY: qemu debug

# Boot via EDK2 UEFI firmware (qemu-efi-riscv64 package).
# Flow: RISCV_VIRT_CODE.fd → BOOTRISCV64.EFI → kernel ELF
AAVMF ?= /usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd
AAVMF_VARS ?= /usr/share/qemu-efi-riscv64/RISCV_VIRT_VARS.fd

qemu: $(BINDIR)/zonix-uefi.img $(BINDIR)/sdcard.img
	@cp -n $(AAVMF_VARS) $(BINDIR)/riscv_vars.fd 2>/dev/null || true
	$(QEMU) -cpu rv64 \
		-drive if=pflash,format=raw,unit=0,file=$(AAVMF),readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BINDIR)/riscv_vars.fd \
		-readconfig qemu-uefi-riscv64.cfg \
		-serial stdio -no-reboot

debug: $(BINDIR)/zonix-uefi.img $(BINDIR)/sdcard.img
	$(QEMU) -cpu rv64 \
		-drive if=pflash,format=raw,unit=0,file=$(AAVMF),readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BINDIR)/riscv_vars.fd \
		-readconfig qemu-uefi-riscv64.cfg \
		-serial stdio -S -s -no-reboot &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit
