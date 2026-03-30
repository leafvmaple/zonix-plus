# ==========================================================================
# arch/x86/Makefile — x86_64 toolchain, flags, and build configuration
#
# Included from the top-level Makefile.
# ==========================================================================

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
BOOT_CXXFLAGS := -g -fno-builtin -Wall -ggdb -O0 -m32 -nostdinc -nostdinc++ \
                 -fno-stack-protector -fno-pic -fno-exceptions -fno-rtti \
                 -fno-use-cxa-atexit -fno-threadsafe-statics \
                 -fno-asynchronous-unwind-tables -fno-unwind-tables \
                 -ffreestanding -std=gnu++17 -gdwarf-2
BOOT_LDFLAGS := -m elf_i386 -nostdlib

DASM    := ndisasm
QEMU    := qemu-system-x86_64

# ---------- kernel source directories ----------
KSRCDIR := kernel              \
           arch/x86/kernel     \
           arch/x86/kernel/drivers \
           kernel/debug        \
           kernel/cons         \
           kernel/trap         \
           kernel/drivers      \
           kernel/block        \
           kernel/sched        \
           kernel/mm           \
           kernel/fs           \
           kernel/fs/fat       \
           kernel/exec         \
           kernel/sync

# ---------- embedded console font (PSF -> ELF .rodata) ----------
FONT_PSF := fonts/console.psf
FONT_OBJ := $(OBJDIR)/fonts/console.psf.o

$(FONT_OBJ): $(FONT_PSF) | $$(dir $$@)
	$(Q)$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--add-section .note.GNU-stack=/dev/null \
		--set-section-flags .note.GNU-stack=contents,readonly \
		$< $@

ALLOBJS += $(FONT_OBJ)

# ---------- kernel link configuration ----------
KERNEL_EXTRA_OBJS := $(FONT_OBJ)
KERNEL_LD_SCRIPT  := $(SCRIPTDIR)/kernel.ld
KERNEL_POST_LINK  :=

# ---------- top-level "all" prerequisites ----------
ALL_PREREQS := $(BINDIR)/mbr $(BINDIR)/vbr $(BINDIR)/bootloader $(BINDIR)/BOOTX64.EFI \
               $(BINDIR)/kernel $(BINDIR)/zonix.img $(BINDIR)/zonix-uefi.img user

# ---------- QEMU run / debug targets ----------
DISK ?= ahci
OVMF ?= /usr/share/ovmf/OVMF.fd

ifeq ($(DISK),ahci)
  QEMU_DATA := -drive file=$(BINDIR)/userdata.img,format=raw,if=none,id=data0 \
               -device ide-hd,bus=ahci0.1,drive=data0
else ifeq ($(DISK),ide)
  QEMU_DATA := -drive file=$(BINDIR)/userdata.img,format=raw,if=ide,index=1
else
  $(error Unknown DISK=$(DISK). Use: ahci, ide)
endif

.PHONY: qemu qemu-bios debug debug-bios disasm

qemu: $(BINDIR)/zonix-uefi.img $(BINDIR)/userdata.img
	$(QEMU) -bios $(OVMF) -readconfig qemu-uefi.cfg $(QEMU_DATA) -no-reboot

qemu-bios: $(BINDIR)/zonix.img $(BINDIR)/userdata.img
	$(QEMU) -readconfig qemu-bios.cfg $(QEMU_DATA) -no-reboot

debug: $(BINDIR)/zonix-uefi.img $(BINDIR)/userdata.img
	$(QEMU) -bios $(OVMF) -readconfig qemu-uefi.cfg $(QEMU_DATA) -S -s -no-reboot &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

debug-bios: $(BINDIR)/zonix.img $(BINDIR)/userdata.img
	$(QEMU) -readconfig qemu-bios.cfg $(QEMU_DATA) -S -s -no-reboot &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit

# ---------- disassembly ----------
disasm: $(BINDIR)/kernel $(BINDIR)/mbr $(BINDIR)/vbr $(BINDIR)/bootloader
	$(Q)$(OBJDUMP) -D $(BINDIR)/kernel     > $(OBJDIR)/kernel.asm
	$(Q)$(DASM) -b 64 $(BINDIR)/kernel.bin > $(OBJDIR)/kernel.disasm 2>/dev/null || true
	$(Q)$(OBJDUMP) -D $(BINDIR)/mbr        > $(OBJDIR)/mbr.asm
	$(Q)$(DASM) -b 16 $(BINDIR)/mbr.bin    > $(OBJDIR)/mbr.disasm 2>/dev/null || true
	$(Q)$(OBJDUMP) -S $(BINDIR)/vbr        > $(OBJDIR)/vbr.asm
	$(Q)$(DASM) -b 16 $(BINDIR)/vbr.bin    > $(OBJDIR)/vbr.disasm 2>/dev/null || true
	$(Q)$(OBJDUMP) -S $(BINDIR)/bootloader > $(OBJDIR)/bootloader.asm
	$(Q)$(DASM) -b 32 $(BINDIR)/bootloader.bin > $(OBJDIR)/bootloader.disasm 2>/dev/null || true
	@echo "  DISASM  $(OBJDIR)/*.asm $(OBJDIR)/*.disasm"
