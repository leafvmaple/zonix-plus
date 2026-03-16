# ==========================================================================
# arch/aarch64/Makefile — AArch64 toolchain, flags, and build configuration
#
# Included from the top-level Makefile.
# ==========================================================================

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

# ---------- kernel source directories ----------
KSRCDIR := kernel              \
           arch/aarch64/kernel \
           arch/aarch64/kernel/drivers \
           kernel/debug        \
           kernel/cons         \
           kernel/sched        \
           kernel/mm           \
           kernel/sync         \
           kernel/block        \
           kernel/fs           \
           kernel/exec         \
           kernel/drivers

# ---------- kernel link configuration ----------
KERNEL_EXTRA_OBJS :=
KERNEL_LD_SCRIPT  := $(SCRIPTDIR)/kernel-aarch64.ld
KERNEL_POST_LINK   = $(Q)$(OBJCOPY) --set-start=0x40080000 $@

# ---------- top-level "all" prerequisites ----------
ALL_PREREQS := $(BINDIR)/kernel

# ---------- QEMU run / debug targets ----------
.PHONY: qemu debug

qemu: $(BINDIR)/kernel
	$(QEMU) -M virt -cpu cortex-a72 -m 256M \
		-nographic -kernel $(BINDIR)/kernel -no-reboot

debug: $(BINDIR)/kernel
	$(QEMU) -M virt -cpu cortex-a72 -m 256M \
		-nographic -kernel $(BINDIR)/kernel -S -s -no-reboot &
	sleep 2
	gdb -q -x $(SCRIPTDIR)/gdbinit
