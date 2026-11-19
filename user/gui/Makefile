# Optional freestanding GUI engine and Zonix platform. The boot and base
# rootfs targets do not depend on this build.
.PHONY: desktop-image
ifeq ($(ARCH),x86)
GUI_OBJDIR := $(USER_OBJDIR)/gui
MINICOCOS_DIR ?= ../mini-cocos
MINICOCOS_REVISION := f58e97052de5a696ad7ed55b99700f265a5b331f
MINICOCOS_SOURCE := $(GUI_OBJDIR)/source
MINICOCOS_ENGINE := $(GUI_OBJDIR)/engine
MINICOCOS_CORE := $(MINICOCOS_ENGINE)/libzocos_core.a
GUI_OBJECTS := $(addprefix $(GUI_OBJDIR)/,math_x86.cpp.o window_channel.cpp.o window_view.cpp.o file_utils.cpp.o mono_font.cpp.o \
                                        controls/style.cpp.o controls/button.cpp.o controls/toolbar.cpp.o \
                                        controls/context_menu.cpp.o controls/icon_grid.cpp.o controls/dialog.cpp.o)
ZONIX_GUI := $(GUI_OBJDIR)/libzonix_gui.a
MINICOCOS_LIB := $(MINICOCOS_ENGINE)/libzocos_software.a
GUI_LIBRARIES := $(ZONIX_GUI) $(MINICOCOS_CORE) $(MINICOCOS_LIB)
MINICOCOS_TARGET_FLAGS := $(filter-out -std=c11 -I%,$(USER_CFLAGS)) -fno-rtti \
                         -fno-threadsafe-statics -fno-use-cxa-atexit -ffunction-sections -fdata-sections
MINICOCOS_CXXFLAGS := $(MINICOCOS_TARGET_FLAGS) -std=$(CXX_STANDARD) -nostdinc++ \
                     -Iinclude -I$(ZCC_DIR)/src/runtime -Iexternal/zstl/include \
                     -DZOCOS_FREESTANDING -fno-exceptions -I"$(MINICOCOS_SOURCE)/src"

$(MINICOCOS_SOURCE)/.prepared: user/gui/mini-cocos.patch user/gui/Makefile
	$(Q)git -C "$(MINICOCOS_DIR)" cat-file -e "$(MINICOCOS_REVISION)^{commit}" || { \
		echo "Set MINICOCOS_DIR to a checkout containing $(MINICOCOS_REVISION)." >&2; exit 1; }
	$(Q)$(MKDIR) "$(MINICOCOS_SOURCE)"
	$(Q)git -C "$(MINICOCOS_DIR)" archive "$(MINICOCOS_REVISION)" CMakeLists.txt cmake src third_party \
		fonts/NotoSans-Regular.ttf fonts/OFL.txt | tar -xf - -C "$(MINICOCOS_SOURCE)"
	$(Q)patch -s -p1 -d "$(MINICOCOS_SOURCE)" < user/gui/mini-cocos.patch
	$(Q)touch $@

$(MINICOCOS_LIB): FORCE $(MINICOCOS_SOURCE)/.prepared
	$(Q)cmake -S "$(MINICOCOS_SOURCE)" -B "$(@D)" -G Ninja \
		-DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
		-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER="$(firstword $(CC))" -DCMAKE_CXX_COMPILER="$(firstword $(CXX))" \
		-DCMAKE_C_COMPILER_TARGET=x86_64-none-elf -DCMAKE_CXX_COMPILER_TARGET=x86_64-none-elf \
		-DCMAKE_AR="$$(command -v $(LLVM_BIN)llvm-ar)" -DCMAKE_RANLIB="$$(command -v $(LLVM_BIN)llvm-ranlib)" \
		-DCMAKE_CXX_FLAGS="$(MINICOCOS_TARGET_FLAGS) -isystem$$($(firstword $(CXX)) -print-resource-dir)/include" \
		-DZSTL_DIR="$(abspath external/zstl)" \
		-DZOCOS_FREESTANDING=ON -DZOCOS_USE_CRT=OFF -DZOCOS_BUILD_HOST=OFF \
		-DZOCOS_BUILD_LUA=OFF -DZOCOS_BUILD_TESTS=OFF -DZOCOS_RENDER_API=SOFTWARE
	$(Q)env -u MAKEFLAGS cmake --build "$(@D)" --target zocos_software zocos_core

$(MINICOCOS_CORE): $(MINICOCOS_LIB)
	$(Q)test -f $@

$(GUI_OBJDIR)/%.cpp.o: user/gui/%.cpp $(MINICOCOS_LIB) $(ZCC_RUNTIME_STAMP) user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(ZONIX_GUI): $(GUI_OBJECTS) user/gui/Makefile
	$(Q)$(RM) $@.tmp
	$(Q)$(LLVM_BIN)llvm-ar rcs $@.tmp $(GUI_OBJECTS)
	$(Q)mv $@.tmp $@

$(GUI_OBJDIR)/desktop_runtime.cpp.o: GUI_HEAP_MIB := 6
$(GUI_OBJDIR)/files_runtime.cpp.o: GUI_HEAP_MIB := 8
$(GUI_OBJDIR)/editor_runtime.cpp.o: GUI_HEAP_MIB := 6
$(GUI_OBJDIR)/terminal_runtime.cpp.o: GUI_HEAP_MIB := 1

$(GUI_OBJDIR)/%_runtime.cpp.o: user/gui/runtime.cpp $(MINICOCOS_LIB) $(ZCC_RUNTIME_STAMP) user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) -DZONIX_GUI_HEAP_MIB=$(GUI_HEAP_MIB) $(DEPFLAGS) -c $< -o $@

DESKTOP_OBJECTS := $(addprefix $(USER_OBJDIR)/desktop/,client.cpp.o compositor.cpp.o desktop.cpp.o pixel_surface.cpp.o \
                                                    chrome/mini_cocos.cpp.o)
TERMINAL_OBJECTS := $(addprefix $(USER_OBJDIR)/terminal/,terminal.cpp.o grid_renderer.cpp.o) $(USER_OBJDIR)/desktop/font.S.o
EDITOR_OBJECTS := $(USER_OBJDIR)/editor/editor.cpp.o $(USER_OBJDIR)/desktop/font.S.o

$(USER_OBJDIR)/editor/%.cpp.o: user/editor/%.cpp $(MINICOCOS_CORE) $(ZCC_RUNTIME_STAMP) user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_OBJDIR)/files/%.cpp.o: user/files/%.cpp $(MINICOCOS_CORE) $(ZCC_RUNTIME_STAMP) user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_OBJDIR)/desktop/%.cpp.o: user/desktop/%.cpp $(MINICOCOS_CORE) $(ZCC_RUNTIME_STAMP) user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) -DZONIX_PIXEL_TERMINAL $(DEPFLAGS) -c $< -o $@

$(USER_OBJDIR)/terminal/%.cpp.o: user/terminal/%.cpp $(MINICOCOS_CORE) $(ZCC_RUNTIME_STAMP) user/gui/Makefile
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CXX) $(MINICOCOS_CXXFLAGS) -DZONIX_PIXEL_TERMINAL $(DEPFLAGS) -c $< -o $@

$(USER_BINDIR)/FILES.ELF: $(USER_OBJDIR)/files/files.cpp.o $(GUI_OBJDIR)/files_runtime.cpp.o
$(USER_BINDIR)/DESKTOP.ELF: $(DESKTOP_OBJECTS) $(GUI_OBJDIR)/desktop_runtime.cpp.o
$(USER_BINDIR)/TERMINAL.ELF: $(TERMINAL_OBJECTS) $(GUI_OBJDIR)/terminal_runtime.cpp.o
$(USER_BINDIR)/EDITOR.ELF: $(EDITOR_OBJECTS) $(GUI_OBJDIR)/editor_runtime.cpp.o
$(USER_BINDIR)/FILES.ELF $(USER_BINDIR)/DESKTOP.ELF $(USER_BINDIR)/TERMINAL.ELF $(USER_BINDIR)/EDITOR.ELF: $(GUI_LIBRARIES) $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR) check-syscall-abi
	@echo "  LINK    $@ (Zonix mini-cocos GUI)"
	$(Q)$(LD) -nostdlib --no-relax --gc-sections --strip-debug -T $(ZCC_SYSROOT)/linker.ld $(ZCC_SYSROOT)/crt0.o \
		$(filter %.o,$^) --start-group $(GUI_LIBRARIES) $(ZCC_SYSROOT)/libzccrt.a --end-group -o $@

DESKTOP_SUITE_ELFS := $(addprefix $(USER_BINDIR)/,DESKTOP.ELF TERMINAL.ELF FILES.ELF EDITOR.ELF)
desktop-image: $(BINDIR)/desktop.img

$(BINDIR)/desktop.img: $(DESKTOP_SUITE_ELFS) $(MINICOCOS_SOURCE)/.prepared scripts/install_userspace_image.sh user/gui/Makefile
	@echo "  IMG     $@"
	$(Q)set -eu; \
		staging=$$(mktemp -d "$(@D)/.desktop.XXXXXXXX"); \
		image="$$staging/desktop.img"; \
		trap 'rm -f "$$image"; rmdir "$$staging"' EXIT; \
		bash scripts/install_userspace_image.sh apps "$$image" $(DESKTOP_SUITE_ELFS); \
		mmd -i "$$image" ::/fonts; \
		mcopy -i "$$image" "$(MINICOCOS_SOURCE)/fonts/NotoSans-Regular.ttf" ::/fonts/UI.TTF; \
		mcopy -i "$$image" "$(MINICOCOS_SOURCE)/fonts/OFL.txt" ::/fonts/OFL.TXT; \
		mv "$$image" "$@"

else
desktop-image:
	@echo "The Desktop suite requires ARCH=x86." >&2
	@exit 1

endif
