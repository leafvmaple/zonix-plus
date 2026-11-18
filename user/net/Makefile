# Optional userspace TCP service and its demonstration worker.
LWIP_DIR ?= ../lwip
LWIP_REVISION := 77dcd25a72509eb83f72b033d219b1d40cd8eb95
TCP_OBJDIR := $(USER_OBJDIR)/net
LWIP_SOURCE := $(TCP_OBJDIR)/source
LWIP_CFLAGS = $(USER_CFLAGS) -ffunction-sections -fdata-sections -Iuser/net/port \
               -I"$(LWIP_SOURCE)/src/include" -isystem$(shell $(firstword $(CC)) -print-resource-dir)/include
LWIP_FILES := core/init core/def core/inet_chksum core/ip core/mem core/memp core/netif core/pbuf \
              core/tcp core/tcp_in core/tcp_out core/timeouts core/ipv4/etharp core/ipv4/ip4 \
              core/ipv4/ip4_addr core/ipv4/icmp netif/ethernet
LWIP_OBJECTS := $(addprefix $(TCP_OBJDIR)/lwip/,$(addsuffix .o,$(LWIP_FILES)))

$(LWIP_SOURCE)/.prepared: user/net/Makefile
	$(Q)git -C "$(LWIP_DIR)" cat-file -e "$(LWIP_REVISION)^{commit}" || { \
		echo "Set LWIP_DIR to an lwIP checkout containing $(LWIP_REVISION)." >&2; exit 1; }
	$(Q)$(MKDIR) "$(@D)"
	$(Q)git -C "$(LWIP_DIR)" archive "$(LWIP_REVISION)" src COPYING | tar -xf - -C "$(@D)"
	$(Q)touch $@

$(LWIP_OBJECTS): $(TCP_OBJDIR)/lwip/%.o: $(LWIP_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP) user/net/Makefile
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(LWIP_CFLAGS) $(DEPFLAGS) -c "$(LWIP_SOURCE)/src/$*.c" -o "$@"

$(TCP_OBJDIR)/tcpserve.o: user/net/tcpserve.c $(LWIP_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP) user/net/Makefile
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(LWIP_CFLAGS) $(DEPFLAGS) -c "$<" -o "$@"

$(TCP_OBJDIR)/tcpecho.o: user/net/tcpecho.c $(ZCC_RUNTIME_STAMP) user/net/Makefile
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c "$<" -o "$@"

$(USER_BINDIR)/TCPSERVE.ELF: $(TCP_OBJDIR)/tcpserve.o $(LWIP_OBJECTS)
$(USER_BINDIR)/TCPECHO.ELF: $(TCP_OBJDIR)/tcpecho.o
$(USER_BINDIR)/TCPSERVE.ELF $(USER_BINDIR)/TCPECHO.ELF: $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR) check-syscall-abi
	$(Q)$(LD) -nostdlib --no-relax --gc-sections -T $(ZCC_SYSROOT)/linker.ld $(ZCC_SYSROOT)/crt0.o \
		$(filter %.o,$^) $(ZCC_SYSROOT)/libzccrt.a -o "$@"

.PHONY: tcp-image
tcp-image: $(USER_BINDIR)/TCPSERVE.ELF $(USER_BINDIR)/TCPECHO.ELF
	$(Q)bash scripts/install_userspace_image.sh apps "$(BINDIR)/tcp.img" $^
	$(Q)mcopy -o -i "$(BINDIR)/tcp.img" "$(LWIP_SOURCE)/COPYING" ::/LWIP.LIC

-include $(LWIP_OBJECTS:.o=.d) $(TCP_OBJDIR)/tcpserve.d $(TCP_OBJDIR)/tcpecho.d
