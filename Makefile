.SECONDEXPANSION:

# ==========================================================================
# Architecture selection
# Usage: make ARCH=x86 (default) | make ARCH=aarch64 | make ARCH=riscv64
# ==========================================================================
ARCH ?= x86
V    ?= 0  # Verbose mode: make V=1

# Quiet / verbose output
ifeq ($(V),0)
  Q := @
  MAKEFLAGS += --no-print-directory
else
  Q :=
endif

# ==========================================================================
# Per-architecture toolchain and flags
# ==========================================================================
ifeq ($(ARCH),x86)
  CC      := clang
  CXX     := clang++
  LD      := ld.lld
  CFLAGS  := -g -fno-builtin -Wall -ggdb -O0 -m64 -mno-red-zone -mcmodel=kernel \
             -mno-sse -mno-sse2 -mno-mmx -msoft-float -nostdinc \
             -fno-stack-protector -fno-pic -gdwarf-2
  CXXFLAGS := -g -Wall -ggdb -O0 -m64 -mno-red-zone -mcmodel=kernel \
             -mno-sse -mno-sse2 -mno-mmx -msoft-float -nostdinc -nostdinc++ \
             -fno-builtin -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
             -fno-use-cxa-atexit -fno-threadsafe-statics \
             -fno-asynchronous-unwind-tables -fno-unwind-tables \
             -ffreestanding -std=gnu++17 -gdwarf-2
  LDFLAGS := -m elf_x86_64 -nostdlib
  BOOT_CFLAGS  := -g -fno-builtin -Wall -ggdb -O0 -m32 -nostdinc -fno-stack-protector -fno-pic -gdwarf-2
  BOOT_LDFLAGS := -m elf_i386 -nostdlib
  DASM    := ndisasm
  QEMU    := qemu-system-x86_64
else ifeq ($(ARCH),aarch64)
  CC      := clang --target=aarch64-none-elf
  CXX     := clang++ --target=aarch64-none-elf
  LD      := ld.lld
  CFLAGS  := -g -Wall -O0 -nostdinc -fno-builtin -fno-stack-protector \
             -fno-pic -ffreestanding -gdwarf-2
  CXXFLAGS := -g -Wall -O0 -nostdinc -nostdinc++ \
             -fno-builtin -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
             -fno-use-cxa-atexit -fno-threadsafe-statics \
             -fno-asynchronous-unwind-tables -fno-unwind-tables \
             -ffreestanding -std=gnu++17 -gdwarf-2
  LDFLAGS := -m aarch64elf -nostdlib
  QEMU    := qemu-system-aarch64
else ifeq ($(ARCH),riscv64)
  CC      := clang --target=riscv64-none-elf
  CXX     := clang++ --target=riscv64-none-elf
  LD      := ld.lld
  CXXFLAGS := -g -Wall -O0 -nostdinc -nostdinc++ \
             -fno-builtin -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
             -fno-use-cxa-atexit -fno-threadsafe-statics \
             -ffreestanding -std=gnu++17 -march=rv64gc -mabi=lp64d
  LDFLAGS := -m elf64lriscv -nostdlib
  QEMU    := qemu-system-riscv64
  $(warning RISC-V 64 support is work-in-progress)
else
  $(error Unsupported ARCH=$(ARCH). Use: x86, aarch64, riscv64)
endif

# Auto-dependency generation (clang/clang++ -MMD -MP)
# .d files are placed next to .o files automatically
DEPFLAGS := -MMD -MP

OBJDUMP := llvm-objdump
OBJCOPY := llvm-objcopy
MKDIR   := mkdir -p

SLASH   := /
OBJDIR  := obj
BINDIR  := bin
SCRIPTDIR := scripts

OBJPREFIX := __objs_
CTYPE     := c S
CXXTYPE   := cpp cc cxx

# ==========================================================================
# Build-system macros
# ==========================================================================

# dirs, #types -> matching source files
listf = $(filter $(if $(2),$(addprefix %.,$(2)),%), $(wildcard $(addsuffix $(SLASH)*,$(1))))
listf_cc  = $(call listf,$(1),$(CTYPE))
listf_cxx = $(call listf,$(1),$(CXXTYPE))

# Packet helpers
packetname = $(if $(1),$(addprefix $(OBJPREFIX),$(1)),$(OBJPREFIX))

# source -> obj path
toobj     = $(addprefix $(OBJDIR)$(SLASH)$(if $(2),$(2)$(SLASH)),$(addsuffix .o,$(basename $(1))))
totarget  = $(addprefix $(BINDIR)$(SLASH),$(1))
tobin     = $(addprefix $(BINDIR)$(SLASH),$(addsuffix .bin,$(1)))

ALLOBJS :=

# Single compile rule (with auto-deps via DEPFLAGS)
# .S files get -D__ASSEMBLY__ so headers can hide C/C++ constructs.
define compile
$$(call toobj,$(1)): $(1) | $$$$(dir $$$$@)
	$(Q)$(2) -I$$(dir $(1)) $(3) $(if $(filter %.S,$(1)),-D__ASSEMBLY__) $(DEPFLAGS) -c $$< -o $$@
ALLOBJS += $$(call toobj,$(1))
endef

compiles = $$(foreach f,$(1),$$(eval $$(call compile,$$(f),$(2),$(3))))

define add_packet
__packet__ := $(call packetname,$(4))
$$(__packet__) += $(call toobj,$(1))
$(call compiles,$(1),$(2),$(3))
endef

read_packet = $(foreach p,$(call packetname,$(1)),$($(p)))

add_packet_files     = $(eval $(call add_packet,$(1),$(2),$(3),$(4)))
add_packet_files_cc  = $(call add_packet_files,$(1),$(CC),$(CFLAGS),$(2))
add_packet_files_cxx = $(call add_packet_files,$(1),$(CXX),$(CXXFLAGS),$(2))

define do_make_dir
$$(sort $$(dir $$(ALLOBJS)) $(BINDIR)$(SLASH) $(OBJDIR)$(SLASH)):
	$(Q)$(MKDIR) $$@
endef
make_dir = $(eval $(call do_make_dir))

# ==========================================================================
# Include paths
# ==========================================================================
#   include/               — architecture-independent headers
#   arch/$(ARCH)/include/  — makes <asm/xxx.h> resolve to the right arch
#   arch/$(ARCH)/kernel/   — arch-specific kernel headers (idt.h, ...)
#   kernel/                — cross-module kernel includes (drivers/, mm/, lib/, ...)
INCLUDE := include \
           arch/$(ARCH)/include \
           arch/$(ARCH)/kernel \
           kernel

# ==========================================================================
# Kernel
# ==========================================================================
ifeq ($(ARCH),x86)
KSRCDIR := kernel              \
           arch/$(ARCH)/kernel \
           kernel/debug        \
           kernel/cons         \
           kernel/trap         \
           kernel/drivers      \
           kernel/block        \
           kernel/sched        \
           kernel/mm           \
           kernel/fs           \
           kernel/exec         \
           kernel/sync
else ifeq ($(ARCH),aarch64)
# AArch64: shared kernel modules + arch-specific replacements.
# arch/aarch64/kernel/ provides: cons.cpp, trap.cpp, drivers.cpp
# that replace x86-specific versions in kernel/cons/, kernel/trap/, kernel/drivers/.
# We include kernel/cons/ and kernel/drivers/ for shared files (shell, stdio, intr)
# but filter out the x86-specific .cpp files in the link step.
KSRCDIR := kernel              \
           arch/$(ARCH)/kernel \
           kernel/debug        \
           kernel/cons         \
           kernel/sched        \
           kernel/mm           \
           kernel/sync         \
           kernel/block        \
           kernel/fs           \
           kernel/exec         \
           kernel/drivers

# Files to exclude from aarch64 build (x86-specific implementations)
AARCH64_EXCLUDE := kernel/cons/cons.cpp   \
                   kernel/trap/trap.cpp   \
                   kernel/drivers/cga.cpp \
                   kernel/drivers/kbd.cpp \
                   kernel/drivers/pic.cpp \
                   kernel/drivers/pit.cpp \
                   kernel/drivers/serial.cpp \
                   kernel/drivers/ide.cpp \
                   kernel/drivers/ide_test.cpp \
                   kernel/drivers/ahci.cpp \
                   kernel/drivers/pci.cpp \
                   kernel/drivers/fbcons.cpp
endif

CFLAGS   += $(addprefix -I,$(INCLUDE))
CXXFLAGS += $(addprefix -I,$(INCLUDE))

$(call add_packet_files_cc,$(call listf_cc,$(KSRCDIR)),kernel)
$(call add_packet_files_cxx,$(call listf_cxx,$(KSRCDIR)),kernel)

kernel = $(call totarget,kernel)
KOBJS  := $(sort $(call read_packet,kernel))

# Filter out architecture-excluded files (aarch64 replaces some x86 modules)
ifdef AARCH64_EXCLUDE
  KOBJS := $(filter-out $(call toobj,$(AARCH64_EXCLUDE)),$(KOBJS))
endif

# Embedded console font (PSF -> ELF .rodata) — x86 only for now
ifeq ($(ARCH),x86)
FONT_PSF := fonts/console.psf
FONT_OBJ := $(OBJDIR)/fonts/console.psf.o

$(FONT_OBJ): $(FONT_PSF) | $$(dir $$@)
	$(Q)$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--add-section .note.GNU-stack=/dev/null \
		--set-section-flags .note.GNU-stack=contents,readonly \
		$< $@

ALLOBJS += $(FONT_OBJ)
KERNEL_EXTRA_OBJS := $(FONT_OBJ)
KERNEL_LD_SCRIPT  := $(SCRIPTDIR)/kernel.ld
else ifeq ($(ARCH),aarch64)
KERNEL_EXTRA_OBJS :=
KERNEL_LD_SCRIPT  := $(SCRIPTDIR)/kernel-aarch64.ld
endif

$(kernel): $(KOBJS) $(KERNEL_EXTRA_OBJS) $(KERNEL_LD_SCRIPT) | $$(dir $$@)
	$(Q)$(LD) $(LDFLAGS) -T $(KERNEL_LD_SCRIPT) $(KOBJS) $(KERNEL_EXTRA_OBJS) -o $@
ifeq ($(ARCH),aarch64)
	$(Q)$(OBJCOPY) --set-start=0x40080000 $@
endif
	$(Q)$(OBJCOPY) -S -O binary $@ $(call tobin,kernel)
	@echo "  LINK    $@"

# ==========================================================================
# Boot (BIOS + UEFI) — separate C/ASM 32-bit toolchain
# ==========================================================================
include arch/$(ARCH)/boot/Makefile

# VBR compatibility alias
BOBJS = $(call toobj,$(bootfiles))

# ==========================================================================
# User-mode programs (separate toolchain, lives on second disk)
# ==========================================================================
include user/Makefile

$(call make_dir)

# ==========================================================================
# Disk images
# ==========================================================================
bin/userdata.img: $(USER_ELFS)
	@echo "  IMG     $@"
	$(Q)bash $(SCRIPTDIR)/create_userdata_image.sh

bin/zonix.img: bin/mbr bin/vbr bin/bootloader bin/kernel | $$(dir $$@)
	@echo "  IMG     $@"
	$(Q)bash $(SCRIPTDIR)/create_zonix_image.sh

bin/zonix-uefi.img: bin/BOOTX64.EFI bin/kernel | $$(dir $$@)
	@echo "  IMG     $@"
	$(Q)bash $(SCRIPTDIR)/create_uefi_image.sh

# ==========================================================================
# Top-level targets
# ==========================================================================
.PHONY: all mbr vbr fat32 uefi clean disasm format lint \
        qemu qemu-ahci qemu-fat32 qemu-uefi qemu-aarch64 \
        debug-qemu debug-qemu-uefi debug-qemu-ahci \
        bochs debug-bochs gdb help

ifeq ($(ARCH),x86)
all: bin/mbr bin/vbr bin/bootloader bin/BOOTX64.EFI bin/kernel bin/zonix.img bin/zonix-uefi.img user
else ifeq ($(ARCH),aarch64)
all: bin/kernel
endif
.DEFAULT_GOAL := all

mbr:  bin/mbr
vbr:  bin/vbr
fat32: bin/zonix.img
uefi: bin/BOOTX64.EFI bin/zonix-uefi.img

# ==========================================================================
# Disassembly (optional — run `make disasm`)
# ==========================================================================
disasm: bin/kernel bin/mbr bin/vbr bin/bootloader
	$(Q)$(OBJDUMP) -D bin/kernel     > obj/kernel.asm
	$(Q)$(DASM) -b 64 bin/kernel.bin > obj/kernel.disasm 2>/dev/null || true
	$(Q)$(OBJDUMP) -D bin/mbr        > obj/mbr.asm
	$(Q)$(DASM) -b 16 bin/mbr.bin    > obj/mbr.disasm 2>/dev/null || true
	$(Q)$(OBJDUMP) -S bin/vbr        > obj/vbr.asm
	$(Q)$(DASM) -b 16 bin/vbr.bin    > obj/vbr.disasm 2>/dev/null || true
	$(Q)$(OBJDUMP) -S bin/bootloader > obj/bootloader.asm
	$(Q)$(DASM) -b 32 bin/bootloader.bin > obj/bootloader.disasm 2>/dev/null || true
	@echo "  DISASM  obj/*.asm obj/*.disasm"

# ==========================================================================
# Code quality (optional)
# ==========================================================================
format:
	@echo "  FORMAT  kernel/ arch/"
	$(Q)find kernel/ arch/ -name '*.cpp' -o -name '*.h' -o -name '*.c' | xargs clang-format -i

lint:
	@echo "  LINT    kernel/ arch/"
	$(Q)find kernel/ arch/ -name '*.cpp' -o -name '*.h' -o -name '*.c' | \
		xargs clang-tidy --quiet -p . 2>/dev/null || true

# ==========================================================================
# QEMU / Bochs / GDB launch targets
# ==========================================================================
qemu: bin/zonix.img bin/userdata.img
	$(QEMU) -readconfig qemu.cfg -no-reboot

qemu-ahci: bin/zonix.img
	$(QEMU) -readconfig qemu-ahci.cfg -S -no-reboot

qemu-fat32: bin/zonix.img
	$(QEMU) -S -no-reboot -readconfig qemu.cfg

qemu-uefi: bin/zonix-uefi.img bin/userdata.img
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -readconfig qemu-uefi.cfg

qemu-aarch64: bin/kernel
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256M \
		-nographic -kernel bin/kernel -no-reboot

debug-qemu: bin/zonix.img
	$(QEMU) -readconfig qemu-debug.cfg -S -s &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

debug-qemu-uefi: bin/zonix-uefi.img
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -readconfig qemu-uefi.cfg -S -s &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

debug-qemu-ahci: bin/zonix.img
	$(QEMU) -S -s -parallel stdio \
		-device ahci,id=ahci0 \
		-drive if=none,id=sata0,file=$<,format=raw \
		-device ide-hd,bus=ahci0.0,drive=sata0 \
		-drive if=ide,index=1,file=bin/userdata.img,format=raw \
		-serial null &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

bochs: bin/zonix.img bin/userdata.img
	bochs -q -f bochsrc.bxrc

debug-bochs: bin/zonix.img bin/userdata.img
	bochs -q -f bochsrc_debug.bxrc -dbg

gdb: bin/zonix.img bin/userdata.img
	bochs -q -f bochsrc.bxrc &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

# ==========================================================================
# Clean
# ==========================================================================
clean: user-clean
	rm -rf obj bin/*.o bin/mbr bin/vbr bin/bootloader bin/kernel \
	       bin/zonix.img bin/userdata.img bin/zonix-uefi.img \
	       bin/BOOTX64.EFI bin/*.bin

# ==========================================================================
# Help
# ==========================================================================
help:
	@echo "Zonix OS build system"
	@echo ""
	@echo "  make [all]           Build kernel + BIOS bootchain + disk image + UEFI"
	@echo "  make bin/kernel      Build kernel only"
	@echo "  make mbr             Build MBR only"
	@echo "  make vbr             Build VBR only"
	@echo "  make fat32           Build FAT32 disk image"
	@echo "  make uefi            Build UEFI bootloader + disk image"
	@echo "  make disasm          Generate disassembly listings in obj/"
	@echo "  make format          Run clang-format on all sources"
	@echo "  make lint            Run clang-tidy on all sources"
	@echo "  make user            Build user-mode programs only"
	@echo "  make qemu            Run in QEMU (BIOS)"
	@echo "  make qemu-uefi       Run in QEMU (UEFI)"
	@echo "  make debug-qemu      Run in QEMU + GDB"
	@echo "  make bochs           Run in Bochs"
	@echo "  make clean           Remove all build artifacts"
	@echo "  make V=1             Verbose build output"
	@echo ""

# ==========================================================================
# Auto-dependency includes (-MMD -MP generated .d files)
# ==========================================================================
-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
