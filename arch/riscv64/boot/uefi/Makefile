# ==========================================================================
# arch/riscv64/boot/uefi/Makefile — RISC-V UEFI boot build rules
#
# LLVM 18 does not support RISC-V COFF output, so we build a position-
# independent ELF shared object and convert it to PE32+ via a Python script
# (scripts/elf2efi.py) that implements the UEFI spec image format.
#
# Machine type 0x5064 = RISC-V 64-bit (UEFI spec §3.1)
# ==========================================================================

BOOT_UEFI_DIR := arch/$(ARCH)/boot/uefi
BOOT_INC      := -I include -I arch/$(ARCH)/include

# Use the bare-metal ELF toolchain (same as the kernel).
UEFI_CXX      := clang++ --target=riscv64-none-elf
UEFI_CXXFLAGS := $(BOOT_INC) -ffreestanding -fshort-wchar -nostdinc \
                  -fno-stack-protector -fno-exceptions -fno-rtti \
                  -fno-use-cxa-atexit -fno-threadsafe-statics \
                  -std=gnu++17 -O2 -march=rv64gc -mabi=lp64d \
                  -mcmodel=medany -fpic -fno-plt $(BOARD_DEFINE)

# Link as a relocatable shared object at VA 0.
UEFI_LD       := ld.lld
UEFI_LD_SCRIPT := scripts/uefi-riscv64.ld
UEFI_LDFLAGS  := -m elf64lriscv -nostdlib -shared -Bsymbolic \
                  -T $(UEFI_LD_SCRIPT)

ELF2EFI       := python3 scripts/elf2efi.py

UEFI_COMMON_SRC := boot/uefi_boot.cpp

UEFI_SO       := $(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.so

bootriscv64 = $(call totarget,BOOTRISCV64.EFI)
$(bootriscv64): $(BOOT_UEFI_DIR)/bootload.cpp $(UEFI_COMMON_SRC) $(UEFI_LD_SCRIPT) | $$(dir $$@)
	$(Q)mkdir -p $(OBJDIR)/$(BOOT_UEFI_DIR) $(OBJDIR)/boot
	$(Q)$(UEFI_CXX) $(UEFI_CXXFLAGS) -c $(BOOT_UEFI_DIR)/bootload.cpp \
		-o $(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.o
	$(Q)$(UEFI_CXX) $(UEFI_CXXFLAGS) -c $(UEFI_COMMON_SRC) \
		-o $(OBJDIR)/boot/uefi_boot.o
	$(Q)$(UEFI_LD) $(UEFI_LDFLAGS) \
		$(OBJDIR)/$(BOOT_UEFI_DIR)/bootload.o $(OBJDIR)/boot/uefi_boot.o \
		-o $(UEFI_SO)
	$(Q)$(ELF2EFI) $(UEFI_SO) $@
