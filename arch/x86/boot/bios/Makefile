# ==========================================================================
# arch/x86/boot/bios/Makefile — BIOS boot build rules
#
# Included from arch/x86/boot/Makefile.  Inherits all top-level variables.
# Toolchain: clang -m32 (32-bit C / ASM, freestanding)
# ==========================================================================

BOOT_BIOS_DIR := arch/$(ARCH)/boot/bios

# --------------------------------------------------------------------------
# MBR (Master Boot Record — 512 bytes, 16-bit real mode)
# --------------------------------------------------------------------------
mbr  = $(call totarget,mbr)
MOBJ = $(call toobj,$(BOOT_BIOS_DIR)/mbr.S)

$(MOBJ): $(BOOT_BIOS_DIR)/mbr.S | $$(dir $$@)
	$(Q)$(CC) $(BOOT_CFLAGS) $(BOOT_INC) $(DEPFLAGS) -D__ASSEMBLY__ -Os -c $< -o $@

$(mbr): $(MOBJ) $(SCRIPTDIR)/mbr.ld | $$(dir $$@)
	$(Q)$(LD) $(BOOT_LDFLAGS) -T $(SCRIPTDIR)/mbr.ld -o $@ $<
	$(Q)$(OBJCOPY) -S -O binary $@ $(call tobin,mbr)
	@if [ `stat -c%s $(call tobin,mbr)` -ne 512 ]; then \
		echo "Error: MBR size is not 512 bytes"; exit 1; fi
	@echo "  MBR     $@"

# --------------------------------------------------------------------------
# VBR (Volume Boot Record — 512 bytes, 16-bit real mode)
# --------------------------------------------------------------------------
bootfiles = $(filter-out $(BOOT_BIOS_DIR)/mbr.S $(BOOT_BIOS_DIR)/bootload.cpp $(BOOT_BIOS_DIR)/entry.S, \
              $(call listf_cc,$(BOOT_BIOS_DIR)))
$(eval $(call compiles,$(bootfiles),$(CC),$(BOOT_CFLAGS) $(BOOT_INC) -Os))

vbr   = $(call totarget,vbr)
VOBJS = $(call toobj,$(bootfiles))

$(vbr): $(VOBJS) $(SCRIPTDIR)/boot.ld | $$(dir $$@)
	$(Q)$(LD) $(BOOT_LDFLAGS) -T $(SCRIPTDIR)/boot.ld -o $@ $(VOBJS)
	$(Q)$(OBJCOPY) -S -O binary $@ $(call tobin,vbr)
	@if [ `stat -c%s $(call tobin,vbr)` -ne 512 ]; then \
		echo "Error: VBR size is `stat -c%s $(call tobin,vbr)` bytes, must be 512"; exit 1; fi
	@echo "  VBR     $@"

# --------------------------------------------------------------------------
# Bootloader (ELF loader at 0x7E00, 32-bit protected mode)
# Can use reserved sectors: sector 1-7 (512 * 7 = 3584 bytes max)
# --------------------------------------------------------------------------
bootloader = $(call totarget,bootloader)
BOOTLOAD_CXX_OBJ := $(call toobj,$(BOOT_BIOS_DIR)/bootload.cpp)
BOOTLOAD_S_OBJ := $(call toobj,$(BOOT_BIOS_DIR)/entry.S)

$(BOOTLOAD_CXX_OBJ): $(BOOT_BIOS_DIR)/bootload.cpp | $$(dir $$@)
	$(Q)$(CXX) $(BOOT_CXXFLAGS) $(BOOT_INC) $(DEPFLAGS) -O2 -c $< -o $@

$(BOOTLOAD_S_OBJ): $(BOOT_BIOS_DIR)/entry.S | $$(dir $$@)
	$(Q)$(CC) $(BOOT_CFLAGS) $(BOOT_INC) $(DEPFLAGS) -D__ASSEMBLY__ -c $< -o $@

ALLOBJS += $(BOOTLOAD_CXX_OBJ) $(BOOTLOAD_S_OBJ)

$(bootloader): $(BOOTLOAD_CXX_OBJ) $(BOOTLOAD_S_OBJ) $(SCRIPTDIR)/bootload.ld | $$(dir $$@)
	$(Q)$(LD) $(BOOT_LDFLAGS) -T $(SCRIPTDIR)/bootload.ld -o $@ $(BOOTLOAD_CXX_OBJ) $(BOOTLOAD_S_OBJ)
	$(Q)$(OBJCOPY) -S -O binary $@ $(call tobin,bootloader)
	@if [ `stat -c%s $(call tobin,bootloader)` -gt 3584 ]; then \
		echo "Error: Bootloader size is `stat -c%s $(call tobin,bootloader)` bytes, must be <= 3584"; exit 1; fi
	@echo "  BOOT    $@"
