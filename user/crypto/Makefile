# Optional static cryptography library for Zonix applications.
MBEDTLS_DIR ?= ../mbedtls
MBEDTLS_REVISION := 82e69999f91126589a28b70dd75b69fbcabe2711
CRYPTO_OBJDIR := $(USER_OBJDIR)/crypto
MBEDTLS_SOURCE := $(CRYPTO_OBJDIR)/source
CRYPTO_CFLAGS = $(USER_CFLAGS) -Iuser/crypto -Iuser/crypto/port -I"$(MBEDTLS_SOURCE)/include" \
                -DMBEDTLS_CONFIG_FILE='"zonix_mbedtls_config.h"' \
                -isystem$(shell $(firstword $(CC)) -print-resource-dir)/include
MBEDTLS_FILES := sha256 chacha20 poly1305 chachapoly constant_time
CRYPTO_OBJECTS := $(CRYPTO_OBJDIR)/zonix_crypto.o $(addprefix $(CRYPTO_OBJDIR)/mbedtls/,$(addsuffix .o,$(MBEDTLS_FILES)))

$(MBEDTLS_SOURCE)/.prepared: user/crypto/Makefile
	$(Q)git -C "$(MBEDTLS_DIR)" cat-file -e "$(MBEDTLS_REVISION)^{commit}" || { \
		echo "Set MBEDTLS_DIR to an Mbed TLS checkout containing $(MBEDTLS_REVISION)." >&2; exit 1; }
	$(Q)$(MKDIR) "$(@D)"
	$(Q)git -C "$(MBEDTLS_DIR)" archive "$(MBEDTLS_REVISION)" include library LICENSE | tar -xf - -C "$(@D)"
	$(Q)touch $@

$(filter $(CRYPTO_OBJDIR)/mbedtls/%,$(CRYPTO_OBJECTS)): $(CRYPTO_OBJDIR)/mbedtls/%.o: $(MBEDTLS_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP)
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(CRYPTO_CFLAGS) $(DEPFLAGS) -c "$(MBEDTLS_SOURCE)/library/$*.c" -o "$@"

$(CRYPTO_OBJDIR)/zonix_crypto.o: user/crypto/zonix_crypto.c $(MBEDTLS_SOURCE)/.prepared $(ZCC_RUNTIME_STAMP)
	$(Q)$(MKDIR) "$(@D)"
	$(Q)$(CC) $(CRYPTO_CFLAGS) $(DEPFLAGS) -c "$<" -o "$@"

$(USER_BINDIR)/libzonixcrypto.a: $(CRYPTO_OBJECTS) | $(USER_BINDIR) check-syscall-abi
	$(Q)$(LLVM_BIN)llvm-ar rcs "$@" $^

.PHONY: crypto
crypto: $(USER_BINDIR)/libzonixcrypto.a

-include $(CRYPTO_OBJECTS:.o=.d)
