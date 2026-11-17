# All C user programs use zcc's frontend, target selection and runtime.
# Standalone assembly programs are retained for architecture register tests.
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
# Used only for the naming check's Clang AST, never to compile user C into ELF.
USER_CFLAGS := -ffreestanding -nostdinc -Iinclude -I$(ZCC_DIR)/src/runtime
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

define user_program_template
_UP_C_$(1) := $$(wildcard $(USER_DIR)/$(1)/*.c)
_UP_ASM_$(1) := $$(wildcard $(USER_DIR)/$(1)/*.S)
ifeq ($$(filter $(1),ssetest ssepeer),)
$(USER_BINDIR)/$(call uc,$(1)).ELF: $$(_UP_C_$(1)) $(ZCC_COMPILER) $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR) check-syscall-abi
	@echo "  ZCC     $$@"
	$(Q)$(ZCC_COMPILER) $(ZCC_TARGET) $$< -o $$@ -sysroot $(ZCC_SYSROOT)
else
# Existing x86 register-state fixtures deliberately ask Clang to emit FP/SIMD.
# They supply their own assembly entry and are never application runtime objects.
_UP_TEST_OBJS_$(1) := $$(patsubst $(USER_DIR)/%,$(USER_OBJDIR)/%.o,$$(_UP_C_$(1)) $$(_UP_ASM_$(1)))
$(USER_OBJDIR)/$(1)/%.c.o: $(USER_DIR)/$(1)/%.c
	$(Q)$(MKDIR) $$(@D)
	$(Q)$(CC) $(USER_TEST_CFLAGS) $(DEPFLAGS) -c $$< -o $$@
$(USER_OBJDIR)/$(1)/%.S.o: $(USER_DIR)/$(1)/%.S
	$(Q)$(MKDIR) $$(@D)
	$(Q)$(CC) $(USER_TEST_CFLAGS) $(DEPFLAGS) -c $$< -o $$@
$(USER_BINDIR)/$(call uc,$(1)).ELF: $$(_UP_TEST_OBJS_$(1)) $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR)
	$(Q)$(LD) -nostdlib -T $(ZCC_SYSROOT)/linker.ld $$(_UP_TEST_OBJS_$(1)) -o $$@
endif
USER_ELFS += $(USER_BINDIR)/$(call uc,$(1)).ELF
endef
$(foreach p,$(USER_PROGRAMS),$(eval $(call user_program_template,$(p))))
.PHONY: user user-image user-clean check-syscall-abi
# Explicitly install normal applications on a separate disk. Never a system-image prerequisite.
user-image: $(USER_BINDIR)/INIT.ELF $(USER_BINDIR)/SH.ELF $(USER_BINDIR)/HELLO.ELF
	$(Q)bash scripts/install_user_image.sh $(BINDIR)/userdata.img $^

user-clean:
	rm -rf $(USER_OBJDIR) $(USER_BINDIR)
check-syscall-abi:
	$(Q)bash scripts/check_syscall_abi_sync.sh

# Preserve zcc's existing sample programs in the data image.
ZCC_TEST_SRCS := $(wildcard $(ZCC_DIR)/test/*.c)
# Template for each zcc test program
# $(1) = source basename without extension, e.g. "hello"
# Output name is prefixed with Z to avoid collision with hand-written user programs.
# Names are stripped of underscores and truncated to 8 chars for FAT32 8.3 compat.
define zcc_program_template

$(USER_BINDIR)/$(call zcc_fat_name,$(1)).ELF: $(ZCC_DIR)/test/$(1).c $(ZCC_COMPILER) $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR)
	@echo "  ZCC     $$@"
	$(Q)$(ZCC_COMPILER) $(ZCC_TARGET) $$< -o $$@ -sysroot $(ZCC_SYSROOT)

USER_ELFS += $(USER_BINDIR)/$(call zcc_fat_name,$(1)).ELF

endef

ifneq ($(ZCC_TARGET),)
  ZCC_PROG_NAMES := $(basename $(notdir $(ZCC_TEST_SRCS)))
  $(foreach p,$(ZCC_PROG_NAMES),$(eval $(call zcc_program_template,$(p))))
endif


user: check-syscall-abi $(USER_ELFS)
