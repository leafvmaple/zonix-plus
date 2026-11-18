# Optional TinySSH worker using tcpserve's standard I/O pipes.
TINYSSH_DIR ?= ../tinyssh
TINYSSH_REVISION := 6a60c2aac18da9035a7401316789d6f4df7e295f
SSH_OBJDIR := $(USER_OBJDIR)/ssh
TINYSSH_SOURCE := $(SSH_OBJDIR)/source
SSH_CFLAGS = $(CRYPTO_CFLAGS) -Iuser/ssh -Iuser/ssh/port -I"$(TINYSSH_SOURCE)" \
             -I"$(TINYSSH_SOURCE)/cryptoint" -fwrapv -ffunction-sections -fdata-sections -DVERSION='"20261101"'
TINYSSH_FILES := buf byte channel cleanup crypto_dh_x25519 crypto_hash_sha512_tinyssh \
                 crypto_onetimeauth_poly1305_tinyssh crypto_scalarmult_curve25519 crypto_sign_ed25519_tinyssh \
                 crypto_stream_chacha20 crypto_verify_16 crypto_verify_32 die e fe fe25519 ge25519 getln global \
                 int16_optblocker int32_optblocker int64_optblocker int8_optblocker log numtostr \
                 packet packet_auth packet_channel_open packet_channel_recv packet_channel_request \
                 packet_channel_send packet_get packet_global_request packet_hello packet_kex packet_kexdh \
                 packet_put packet_recv packet_send packet_unimplemented packetparser randommod sc25519 \
                 ssh sshcrypto sshcrypto_cipher sshcrypto_cipher_chachapoly sshcrypto_kex \
                 sshcrypto_kex_curve25519 sshcrypto_key sshcrypto_key_ed25519 str stringparser \
                 uint16_optblocker uint32_optblocker uint64_optblocker uint8_optblocker writeall
TINYSSH_OBJECTS := $(addprefix $(SSH_OBJDIR)/tinyssh/,$(addsuffix .o,$(TINYSSH_FILES)))
SSH_OBJECTS := $(SSH_OBJDIR)/server.o $(SSH_OBJDIR)/zonix_port.o $(TINYSSH_OBJECTS)

$(TINYSSH_SOURCE)/.prepared: user/ssh/Makefile user/ssh/tinyssh.patch
	$(Q)git -C "$(TINYSSH_DIR)" cat-file -e "$(TINYSSH_REVISION)^{commit}" || { \
		echo "Set TINYSSH_DIR to a TinySSH checkout containing $(TINYSSH_REVISION)." >&2; exit 1; }
	$(Q)$(MKDIR) "$(@D)"
	$(Q)git -C "$(TINYSSH_DIR)" archive "$(TINYSSH_REVISION)" | tar -xf - -C "$(@D)"
	$(Q)patch -s -f -p1 -d "$(@D)" < user/ssh/tinyssh.patch
	$(Q)for feature in haslib1305 haslib25519 haslibntruprime haslibrandombytes haslimits hasvalgrind; do \
		: > "$(@D)/$$feature.h"; done
	$(Q)touch $@

$(TINYSSH_OBJECTS): $(SSH_OBJDIR)/tinyssh/%.o: $(TINYSSH_SOURCE)/.prepared $(MBEDTLS_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP)
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(SSH_CFLAGS) $(DEPFLAGS) -c "$(TINYSSH_SOURCE)/$*.c" -o "$@"

$(SSH_OBJDIR)/%.o: user/ssh/%.c $(TINYSSH_SOURCE)/.prepared $(MBEDTLS_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP)
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(SSH_CFLAGS) $(DEPFLAGS) -c "$<" -o "$@"

$(USER_BINDIR)/SSHD.ELF: $(SSH_OBJECTS) $(USER_BINDIR)/libzonixcrypto.a $(ZCC_RUNTIME_STAMP) | $(USER_BINDIR) check-syscall-abi
	$(Q)$(LD) -nostdlib --no-relax --gc-sections -T $(ZCC_SYSROOT)/linker.ld $(ZCC_SYSROOT)/crt0.o \
		$(filter %.o %.a,$^) $(ZCC_SYSROOT)/libzccrt.a -o "$@"

.PHONY: ssh ssh-image
ssh: $(USER_BINDIR)/SSHD.ELF

ssh-image: $(USER_BINDIR)/TCPSERVE.ELF $(USER_BINDIR)/SSHD.ELF
	$(Q)bash scripts/install_ssh_image.sh "$(BINDIR)/ssh.img" "$(SSH_HOSTKEY_DIR)" "$(SSH_AUTHORIZED_KEY)" \
		"$(TINYSSH_SOURCE)/LICENSE.md" "$(MBEDTLS_SOURCE)/LICENSE" "$(LWIP_SOURCE)/COPYING" $^

-include $(SSH_OBJECTS:.o=.d)
