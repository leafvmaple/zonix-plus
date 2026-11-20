# Optional network service (netd), its socket library and the network programs.
LWIP_DIR ?= ../lwip
LWIP_REVISION := 77dcd25a72509eb83f72b033d219b1d40cd8eb95
NET_OBJDIR := $(USER_OBJDIR)/net
LWIP_SOURCE := $(NET_OBJDIR)/source
LWIP_CFLAGS = $(USER_CFLAGS) -ffunction-sections -fdata-sections -Iuser/net/port \
               -I"$(LWIP_SOURCE)/src/include" -isystem$(shell $(firstword $(CC)) -print-resource-dir)/include
LWIP_FILES := core/init core/def core/dns core/inet_chksum core/ip core/mem core/memp core/netif core/pbuf \
              core/raw core/tcp core/tcp_in core/tcp_out core/timeouts core/udp core/ipv4/dhcp core/ipv4/etharp \
              core/ipv4/ip4 core/ipv4/ip4_addr core/ipv4/icmp netif/ethernet
LWIP_OBJECTS := $(addprefix $(NET_OBJDIR)/lwip/,$(addsuffix .o,$(LWIP_FILES)))
NET_LIBRARY := $(NET_OBJDIR)/zonix_net.o
NET_CLIENTS := ping nc netinfo tcpserve fetch
NET_PROGRAMS := netd $(NET_CLIENTS) tcpecho
NET_ELFS := $(foreach p,$(NET_PROGRAMS),$(USER_BINDIR)/$(call uc,$(p)).ELF)

$(LWIP_SOURCE)/.prepared: user/net/Makefile
	$(Q)git -C "$(LWIP_DIR)" cat-file -e "$(LWIP_REVISION)^{commit}" || { \
		echo "Set LWIP_DIR to an lwIP checkout containing $(LWIP_REVISION)." >&2; exit 1; }
	$(Q)$(MKDIR) "$(@D)"
	$(Q)git -C "$(LWIP_DIR)" archive "$(LWIP_REVISION)" src COPYING | tar -xf - -C "$(@D)"
	$(Q)touch $@

$(LWIP_OBJECTS): $(NET_OBJDIR)/lwip/%.o: $(LWIP_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP) user/net/Makefile
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(LWIP_CFLAGS) $(DEPFLAGS) -c "$(LWIP_SOURCE)/src/$*.c" -o "$@"

$(NET_OBJDIR)/netd.o: user/net/netd.c $(LWIP_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP) user/net/Makefile
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(LWIP_CFLAGS) $(DEPFLAGS) -c "$<" -o "$@"

$(NET_LIBRARY) $(addprefix $(NET_OBJDIR)/,$(addsuffix .o,$(NET_CLIENTS) tcpecho)): \
		$(NET_OBJDIR)/%.o: user/net/%.c $(ZCC_RUNTIME_STAMP) user/net/Makefile
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c "$<" -o "$@"

$(USER_BINDIR)/NETD.ELF: $(NET_OBJDIR)/netd.o $(NET_LIBRARY) $(LWIP_OBJECTS)
$(USER_BINDIR)/TCPECHO.ELF: $(NET_OBJDIR)/tcpecho.o
$(foreach p,$(NET_CLIENTS),$(eval $(USER_BINDIR)/$(call uc,$(p)).ELF: $(NET_OBJDIR)/$(p).o $(NET_LIBRARY)))
$(NET_ELFS): $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR) check-syscall-abi
	$(Q)$(LD) -nostdlib --no-relax --gc-sections --strip-debug --discard-all -T $(ZCC_SYSROOT)/linker.ld $(ZCC_SYSROOT)/crt0.o \
		$(filter %.o,$^) $(ZCC_SYSROOT)/libzccrt.a -o "$@"

# NETWORK_IMAGE may name an existing application disk, such as desktop.img, to add networking to it.
NETWORK_IMAGE ?= $(BINDIR)/network.img
.PHONY: network-image
network-image: $(NET_ELFS)
	$(Q)bash scripts/install_network_image.sh "$(NETWORK_IMAGE)" "$(LWIP_SOURCE)/COPYING" $^

-include $(LWIP_OBJECTS:.o=.d) $(patsubst %,$(NET_OBJDIR)/%.d,$(NET_PROGRAMS) zonix_net)
