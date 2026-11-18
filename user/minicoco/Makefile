# The rendering example uses the software library and shared window channel.
.PHONY: minicocos-demo
ifeq ($(ARCH),x86)
MINICOCOS_OBJDIR := $(USER_OBJDIR)/minicoco

minicocos-demo: $(USER_BINDIR)/MINICOCO.ELF

$(MINICOCOS_OBJDIR)/%.cpp.o: user/minicoco/%.cpp $(MINICOCOS_LIB) $(ZCC_RUNTIME_STAMP) user/minicoco/Makefile user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_BINDIR)/MINICOCO.ELF: $(MINICOCOS_OBJDIR)/demo.cpp.o $(MINICOCOS_OBJDIR)/arena.cpp.o \
                            $(GUI_OBJDIR)/window_channel.cpp.o \
                            $(MINICOCOS_LIB) $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR) check-syscall-abi
	@echo "  LINK    $@ (mini-cocos software renderer)"
	$(Q)$(LD) -nostdlib --no-relax -T $(ZCC_SYSROOT)/linker.ld $(ZCC_SYSROOT)/crt0.o \
		$(filter-out $(ZCC_RUNTIME_STAMP),$^) $(ZCC_SYSROOT)/libzccrt.a -o $@

else
minicocos-demo:
	@echo "The mini-cocos demo requires ARCH=x86." >&2
	@exit 1
endif
