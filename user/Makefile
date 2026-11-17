# Base components use Clang; applications can select zcc or a native C compiler.
# Both share the existing startup, syscall wrappers, library and linker script.
# Assembly register fixtures retain their dedicated entry points.
BASE_PROGRAMS := init sh ls cat cp mkdir rm sync shutdown ps kill
USER_DIR := user
USER_BINDIR := $(BINDIR)/user
USER_OBJDIR := $(OBJDIR)/user
ZCC_DIR := $(USER_DIR)/zcc
ZCC_COMPILER := $(ZCC_DIR)/build/compiler
ZCC_LIBARCH := $(if $(filter x86,$(ARCH)),x64,$(ARCH))
ZCC_TARGET := -$(ZCC_LIBARCH)
ZCC_SYSROOT := $(ZCC_DIR)/lib/$(ZCC_LIBARCH)
ZCC_RUNTIME_STAMP := $(ZCC_SYSROOT)/.runtime
ZCC_SOURCES := $(shell find $(ZCC_DIR)/src $(ZCC_DIR)/parser -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.l' -o -name '*.y' \))
ZCC_RUNTIME_SOURCES := $(shell find $(ZCC_DIR)/src/runtime -type f)
# zcc remains the default application path. "clang" uses the target Clang driver;
# a cross-GCC (or other compatible C compiler) may be supplied as USER_CC instead.
USER_CC ?= zcc
USER_NATIVE_CC := $(USER_CC)
ifeq ($(USER_CC),clang)
USER_NATIVE_CC := $(CC)
endif
USER_COMPILER_STAMP := $(USER_OBJDIR)/.compiler
# A frontend switch invalidates outputs even within one timestamp tick (make 3.81).
ifneq ($(shell cat $(USER_COMPILER_STAMP) 2>/dev/null),$(USER_CC))
USER_COMPILER_CHANGED := FORCE
endif
USER_CFLAGS := -g -O2 -Wall -Wextra -std=c11 -ffreestanding -nostdinc \
               -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
               -fno-unwind-tables -fno-asynchronous-unwind-tables \
               $(USER_ARCH_CFLAGS) -Iinclude -I$(ZCC_DIR)/src/runtime
USER_ASFLAGS := -g -nostdinc -ffreestanding
USER_PROGRAMS := $(notdir $(patsubst %/,%,$(dir $(wildcard $(USER_DIR)/*/*.S $(USER_DIR)/*/*.c))))
USER_PROGRAMS := $(filter-out zcc,$(sort $(USER_PROGRAMS)))
ifneq ($(ARCH),x86)
USER_PROGRAMS := $(filter-out ssetest ssepeer,$(USER_PROGRAMS))
endif
ifneq ($(TEST),1)
USER_PROGRAMS := $(filter-out p1life ssetest ssepeer,$(USER_PROGRAMS))
endif
uc = $(shell echo '$(1)' | tr '[:lower:]' '[:upper:]')
zcc_fat_name = $(shell echo 'Z$(1)' | tr '[:lower:]' '[:upper:]' | tr -d '_' | head -c 8)

$(ZCC_COMPILER): $(ZCC_SOURCES) $(ZCC_DIR)/makefile
	$(Q)$(MAKE) -C $(ZCC_DIR) all

$(ZCC_RUNTIME_STAMP): $(ZCC_RUNTIME_SOURCES) include/abi/syscall.h include/abi/process.h include/abi/fcntl.h
	$(Q)$(MAKE) -C $(ZCC_DIR)/src/runtime $(ZCC_LIBARCH) ABI_DIR=$(abspath include/abi)
	$(Q)touch $@

$(USER_BINDIR):
	$(Q)$(MKDIR) $@

# Like the kernel's TEST-mode stamp: changing frontend must rebuild app outputs.
$(USER_COMPILER_STAMP): FORCE
	$(Q)$(MKDIR) $(@D)
	$(Q)if [ ! -f $@ ] || [ "$$(cat $@)" != "$(USER_CC)" ]; then echo "$(USER_CC)" > $@; fi

define user_program_template
_UP_C_$(1) := $$(wildcard $(USER_DIR)/$(1)/*.c)
_UP_ASM_$(1) := $$(wildcard $(USER_DIR)/$(1)/*.S)
_UP_COMPILER_DEP_$(1) := $$(if $$(filter $(BASE_PROGRAMS) ssetest ssepeer,$(1)),,$(USER_COMPILER_STAMP) $(USER_COMPILER_CHANGED))
ifneq ($$(and $$(filter-out $(BASE_PROGRAMS) ssetest ssepeer,$(1)),$$(filter zcc,$(USER_CC))),)
$(USER_BINDIR)/$(call uc,$(1)).ELF: $$(_UP_C_$(1)) $(ZCC_COMPILER) $(ZCC_RUNTIME_STAMP) $$(_UP_COMPILER_DEP_$(1)) user/Makefile | $(USER_BINDIR) check-syscall-abi
	@echo "  ZCC     $$@"
	$(Q)$(ZCC_COMPILER) $(ZCC_TARGET) $$< -o $$@ -sysroot $(ZCC_SYSROOT)
else
_UP_CC_$(1) := $$(if $$(filter $(BASE_PROGRAMS) ssetest ssepeer,$(1)),$(CC),$(USER_NATIVE_CC))
_UP_FLAGS_$(1) := $$(if $$(filter ssetest ssepeer,$(1)),$(USER_TEST_CFLAGS),$(USER_CFLAGS))
_UP_OBJS_$(1) := $$(patsubst $(USER_DIR)/%,$(USER_OBJDIR)/%.o,$$(_UP_C_$(1)) $$(_UP_ASM_$(1)))
$(USER_OBJDIR)/$(1)/%.c.o: $(USER_DIR)/$(1)/%.c $(ZCC_RUNTIME_STAMP) $$(_UP_COMPILER_DEP_$(1)) user/Makefile arch/$(ARCH)/Makefile
	$(Q)$(MKDIR) $$(@D)
	$(Q)$$(_UP_CC_$(1)) $$(_UP_FLAGS_$(1)) $(DEPFLAGS) -c $$< -o $$@
$(USER_OBJDIR)/$(1)/%.S.o: $(USER_DIR)/$(1)/%.S $(ZCC_RUNTIME_STAMP) $$(_UP_COMPILER_DEP_$(1)) user/Makefile arch/$(ARCH)/Makefile
	$(Q)$(MKDIR) $$(@D)
	$(Q)$$(_UP_CC_$(1)) $$(_UP_FLAGS_$(1)) $(DEPFLAGS) -c $$< -o $$@
$(USER_BINDIR)/$(call uc,$(1)).ELF: $$(_UP_OBJS_$(1)) $(ZCC_RUNTIME_STAMP) $$(_UP_COMPILER_DEP_$(1)) | $(USER_BINDIR) check-syscall-abi
ifeq ($$(filter $(1),ssetest ssepeer),)
	@echo "  LINK    $$@ (shared user runtime)"
	$(Q)$(LD) -nostdlib --no-relax -T $(ZCC_SYSROOT)/linker.ld $(ZCC_SYSROOT)/crt0.o $$(_UP_OBJS_$(1)) $(ZCC_SYSROOT)/libzccrt.a -o $$@
else
# Existing register fixtures supply their own assembly entry.
	$(Q)$(LD) -nostdlib -T $(ZCC_SYSROOT)/linker.ld $$(_UP_OBJS_$(1)) -o $$@
endif
endif
ifeq ($$(filter $(1),$(BASE_PROGRAMS)),)
USER_ELFS += $(USER_BINDIR)/$(call uc,$(1)).ELF
endif
endef
$(foreach p,$(USER_PROGRAMS),$(eval $(call user_program_template,$(p))))
.PHONY: user rootfs user-image zcc-examples user-clean check-syscall-abi
# Explicit installations; neither is a boot-image prerequisite.
rootfs: $(foreach p,$(BASE_PROGRAMS),$(USER_BINDIR)/$(call uc,$(p)).ELF)
	$(Q)bash scripts/install_userspace_image.sh base $(BINDIR)/rootfs.img $^

user-image: $(USER_BINDIR)/HELLO.ELF
	$(Q)bash scripts/install_userspace_image.sh apps $(BINDIR)/apps.img $^

user-clean:
	rm -rf $(USER_OBJDIR) $(USER_BINDIR)
check-syscall-abi:
	$(Q)bash scripts/check_syscall_abi_sync.sh

# Compiler examples have an explicit target; native application builds need no zcc frontend.
ZCC_TEST_SRCS := $(wildcard $(ZCC_DIR)/test/*.c)
# Template for each zcc test program
# $(1) = source basename without extension, e.g. "hello"
# Output name is prefixed with Z to avoid collision with hand-written user programs.
# Names are stripped of underscores and truncated to 8 chars for FAT32 8.3 compat.
define zcc_program_template

$(USER_BINDIR)/$(call zcc_fat_name,$(1)).ELF: $(ZCC_DIR)/test/$(1).c $(ZCC_COMPILER) $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR)
	@echo "  ZCC     $$@"
	$(Q)$(ZCC_COMPILER) $(ZCC_TARGET) $$< -o $$@ -sysroot $(ZCC_SYSROOT)

ZCC_EXAMPLE_ELFS += $(USER_BINDIR)/$(call zcc_fat_name,$(1)).ELF

endef

ifneq ($(ZCC_TARGET),)
  ZCC_PROG_NAMES := $(basename $(notdir $(ZCC_TEST_SRCS)))
  $(foreach p,$(ZCC_PROG_NAMES),$(eval $(call zcc_program_template,$(p))))
endif


user: check-syscall-abi $(USER_ELFS)
zcc-examples: check-syscall-abi $(ZCC_EXAMPLE_ELFS)

-include $(wildcard $(USER_OBJDIR)/*/*.d)
