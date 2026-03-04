.SECONDEXPANSION:

# ==========================================================================
# Architecture selection (x86 for now, aarch64 in the future)
# Usage: make ARCH=x86 (default)
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
  CC      := gcc
  CXX     := g++
  LD      := ld
  CFLAGS  := -g -fno-builtin -Wall -ggdb -O0 -m64 -mno-red-zone -mcmodel=kernel \
             -mno-sse -mno-sse2 -mno-mmx -msoft-float -nostdinc \
             -fno-stack-protector -fno-PIC -gdwarf-2
  CXXFLAGS := -g -Wall -ggdb -O0 -m64 -mno-red-zone -mcmodel=kernel \
             -mno-sse -mno-sse2 -mno-mmx -msoft-float -nostdinc -nostdinc++ \
             -fno-builtin -fno-stack-protector -fno-PIC -fno-exceptions -fno-rtti \
             -fno-use-cxa-atexit -fno-threadsafe-statics \
             -fno-asynchronous-unwind-tables -fno-unwind-tables \
             -ffreestanding -std=gnu++17 -gdwarf-2
  LDFLAGS := -m elf_x86_64 -nostdlib
  BOOT_CFLAGS  := -g -fno-builtin -Wall -ggdb -O0 -m32 -nostdinc -fno-stack-protector -fno-PIC -gdwarf-2
  BOOT_LDFLAGS := -m elf_i386 -nostdlib
  DASM    := ndisasm
  QEMU    := qemu-system-x86_64
else ifeq ($(ARCH),aarch64)
  $(error ARM/AArch64 support is not yet implemented)
else
  $(error Unsupported ARCH=$(ARCH). Use: x86, aarch64)
endif

# Auto-dependency generation (gcc/g++ -MMD -MP)
# .d files are placed next to .o files automatically
DEPFLAGS := -MMD -MP

HOSTCC     := gcc
HOSTCFLAGS := -g -Wall -O2

OBJDUMP := objdump
OBJCOPY := objcopy
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
define compile
$$(call toobj,$(1)): $(1) | $$$$(dir $$$$@)
	$(Q)$(2) -I$$(dir $(1)) $(3) $(DEPFLAGS) -c $$< -o $$@
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
#   arch/$(ARCH)/kernel/   — arch-specific kernel headers (idt.h, e820.h, ...)
#   kernel/                — cross-module kernel includes (drivers/, mm/, lib/, ...)
INCLUDE := include \
           arch/$(ARCH)/include \
           arch/$(ARCH)/kernel \
           kernel

# ==========================================================================
# Kernel
# ==========================================================================
KSRCDIR := kernel              \
           arch/$(ARCH)/kernel \
           kernel/debug        \
           kernel/cons         \
           kernel/trap         \
           kernel/drivers      \
           kernel/block        \
           kernel/sched        \
           kernel/mm           \
           kernel/fs

CFLAGS   += $(addprefix -I,$(INCLUDE))
CXXFLAGS += $(addprefix -I,$(INCLUDE))

$(call add_packet_files_cc,$(call listf_cc,$(KSRCDIR)),kernel)
$(call add_packet_files_cxx,$(call listf_cxx,$(KSRCDIR)),kernel)

kernel = $(call totarget,kernel)
KOBJS  := $(sort $(call read_packet,kernel))

# Embedded console font (PSF -> ELF .rodata)
FONT_PSF := fonts/console.psf
FONT_OBJ := $(OBJDIR)/fonts/console.psf.o

$(FONT_OBJ): $(FONT_PSF) | $$(dir $$@)
	$(Q)$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		$< $@

ALLOBJS += $(FONT_OBJ)

$(kernel): $(KOBJS) $(FONT_OBJ) $(SCRIPTDIR)/kernel.ld | $$(dir $$@)
	$(Q)$(LD) $(LDFLAGS) -T $(SCRIPTDIR)/kernel.ld $(KOBJS) $(FONT_OBJ) -o $@
	$(Q)$(OBJCOPY) -S -O binary $@ $(call tobin,kernel)
	@echo "  LINK    $@"

# ==========================================================================
# Boot (BIOS + UEFI) — separate C/ASM 32-bit toolchain
# ==========================================================================
include arch/$(ARCH)/boot/Makefile

# VBR compatibility alias
BOBJS = $(call toobj,$(bootfiles))

$(call make_dir)

# ==========================================================================
# Disk images
# ==========================================================================
bin/fat32_test.img:
	@echo "  IMG     $@"
	$(Q)bash $(SCRIPTDIR)/create_fat32_image.sh

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
        qemu qemu-ahci qemu-fat32 qemu-uefi \
        debug-qemu debug-qemu-uefi debug-qemu-ahci \
        bochs debug-bochs gdb help

all: bin/mbr bin/vbr bin/bootloader bin/kernel bin/zonix.img bin/BOOTX64.EFI
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
qemu: bin/zonix.img bin/fat32_test.img
	$(QEMU) -readconfig qemu.cfg -no-reboot

qemu-ahci: bin/zonix.img
	$(QEMU) -readconfig qemu-ahci.cfg -S -no-reboot

qemu-fat32: bin/zonix.img
	$(QEMU) -S -no-reboot -readconfig qemu.cfg

qemu-uefi: bin/zonix-uefi.img bin/fat32_test.img
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -readconfig qemu-uefi.cfg

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
		-drive if=ide,index=1,file=bin/fat32_test.img,format=raw \
		-serial null &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

bochs: bin/zonix.img bin/fat32_test.img
	bochs -q -f bochsrc.bxrc

debug-bochs: bin/zonix.img bin/fat32_test.img
	bochs -q -f bochsrc_debug.bxrc -dbg

gdb: bin/zonix.img bin/fat32_test.img
	bochs -q -f bochsrc.bxrc &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

# ==========================================================================
# Clean
# ==========================================================================
clean:
	rm -rf obj bin/*.o bin/mbr bin/vbr bin/bootloader bin/kernel \
	       bin/zonix.img bin/fat32_test.img bin/zonix-uefi.img \
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
