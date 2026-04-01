.SECONDEXPANSION:

# ==========================================================================
# Architecture selection
# Usage: make ARCH=x86 (default) | make ARCH=aarch64
# ==========================================================================
ARCH ?= x86
V    ?= 0  # Verbose mode: make V=1
# Build kernel test suites: make TEST=1
TEST ?= 0

# Quiet / verbose output
ifeq ($(V),0)
  Q := @
  MAKEFLAGS += --no-print-directory
else
  Q :=
endif

# Auto-dependency generation (clang/clang++ -MMD -MP)
# .d files are placed next to .o files automatically
DEPFLAGS := -MMD -MP

OBJDUMP := llvm-objdump
OBJCOPY := llvm-objcopy
MKDIR   := mkdir -p

SLASH   := /
OBJDIR  := obj/$(ARCH)
BINDIR  := bin/$(ARCH)
SCRIPTDIR := scripts

OBJPREFIX := __objs_
CTYPE     := c S
CXXTYPE   := cpp cc cxx

ALLOBJS :=

# ==========================================================================
# Per-architecture toolchain, flags, and source directories
# ==========================================================================
ifeq ($(wildcard arch/$(ARCH)/Makefile),)
  $(error Unsupported ARCH=$(ARCH). Available: $(patsubst arch/%/Makefile,%,$(wildcard arch/*/Makefile)))
endif
include arch/$(ARCH)/Makefile

ifeq ($(TEST),1)
	KSRCDIR += kernel/test \
	           kernel/test/unit/sched kernel/test/unit/mm kernel/test/shell \
	           kernel/test/unit/lib kernel/test/unit/block \
	           kernel/test/unit/exec kernel/test/unit/cons \
	           kernel/test/unit/fs
	CFLAGS   += -DTEST_MODE=1
	CXXFLAGS += -DTEST_MODE=1
	ifeq ($(ARCH),x86)
		KSRCDIR += kernel/test/unit/drivers
	endif
endif

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

# Single compile rule (with auto-deps via DEPFLAGS)
# .S files get -D__ASSEMBLY__ so headers can hide C/C++ constructs.
define compile
$$(call toobj,$(1)): $(1) $$(TEST_MODE_STAMP) | $$$$(dir $$$$@)
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
CFLAGS   += $(addprefix -I,$(INCLUDE))
CXXFLAGS += $(addprefix -I,$(INCLUDE))

# Track TEST mode changes: when TEST switches, all .o files must be recompiled
# (because -DTEST_MODE=1 changes preprocessor output).  Must be defined before
# the compile rules are eval'd by add_packet_files_cxx below.
TEST_MODE_STAMP := $(OBJDIR)/.test_mode

$(call add_packet_files_cc,$(call listf_cc,$(KSRCDIR)),kernel)
$(call add_packet_files_cxx,$(call listf_cxx,$(KSRCDIR)),kernel)

kernel = $(call totarget,kernel)
KOBJS  := $(sort $(call read_packet,kernel))

FORCE:

$(TEST_MODE_STAMP): FORCE | $(OBJDIR)/
	$(Q)if [ ! -f $@ ] || [ "$$(cat $@)" != "$(TEST)" ]; then echo "$(TEST)" > $@; fi

$(kernel): $(KOBJS) $(KERNEL_EXTRA_OBJS) $(KERNEL_LD_SCRIPT) $(TEST_MODE_STAMP) | $$(dir $$@)
	$(Q)$(LD) $(LDFLAGS) -T $(KERNEL_LD_SCRIPT) $(KOBJS) $(KERNEL_EXTRA_OBJS) -o $@
	$(if $(KERNEL_POST_LINK),$(KERNEL_POST_LINK))
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
$(BINDIR)/userdata.img: $(USER_ELFS)
	@echo "  IMG     $@"
	$(Q)BINDIR=$(BINDIR) bash $(SCRIPTDIR)/create_userdata_image.sh

$(BINDIR)/zonix.img: $(BINDIR)/mbr $(BINDIR)/vbr $(BINDIR)/bootloader $(BINDIR)/kernel | $$(dir $$@)
	@echo "  IMG     $@"
	$(Q)BINDIR=$(BINDIR) bash $(SCRIPTDIR)/create_zonix_image.sh

ifeq ($(ARCH),x86)
$(BINDIR)/zonix-uefi.img: $(BINDIR)/BOOTX64.EFI $(BINDIR)/kernel | $$(dir $$@)
	@echo "  IMG     $@"
	$(Q)BINDIR=$(BINDIR) bash $(SCRIPTDIR)/create_uefi_image.sh
endif

# ==========================================================================
# Top-level targets
# ==========================================================================
.PHONY: all clean format lint compdb help FORCE

all: $(ALL_PREREQS)
.DEFAULT_GOAL := all

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

compdb:
	@command -v bear >/dev/null 2>&1 || { echo "bear not found — install with: sudo apt install bear"; exit 1; }
	bear -- $(MAKE) -B all
	@echo "  COMPDB  compile_commands.json"

# ==========================================================================
# Clean
# ==========================================================================
clean: user-clean
	rm -rf obj/$(ARCH) bin/$(ARCH)

# ==========================================================================
# Help
# ==========================================================================
help:
	@echo "Zonix OS build system  (ARCH=$(ARCH))"
	@echo ""
	@echo "Build:"
	@echo "  make [all]           Build everything for current ARCH"
	@echo "  make bin/kernel      Build kernel only"
	@echo "  make user            Build user-mode programs"
	@echo "  make disasm          Generate disassembly listings (x86)"
	@echo "  make clean           Remove all build artifacts"
	@echo ""
	@echo "Run (x86):"
	@echo "  make qemu            UEFI + AHCI (default)"
	@echo "  make qemu-bios       BIOS + IDE fallback"
	@echo "  make qemu DISK=ide   Use IDE for user-data disk"
	@echo "  make debug           UEFI + AHCI + GDB"
	@echo "  make debug-bios      BIOS + IDE + GDB"
	@echo ""
	@echo "Run (aarch64):"
	@echo "  make qemu ARCH=aarch64"
	@echo ""
	@echo "Quality:"
	@echo "  make format          Run clang-format on all sources"
	@echo "  make lint            Run clang-tidy on all sources"
	@echo "  make compdb          Generate compile_commands.json (needs bear)"
	@echo ""
	@echo "Options:"
	@echo "  ARCH=x86|aarch64     Target architecture (default: x86)"
	@echo "  DISK=ahci|ide        User-data disk controller (default: ahci)"
	@echo "  TEST=0|1             Include kernel test suites (default: 0)"
	@echo "  V=1                  Verbose build output"
	@echo ""

# ==========================================================================
# Auto-dependency includes (-MMD -MP generated .d files)
# ==========================================================================
-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
