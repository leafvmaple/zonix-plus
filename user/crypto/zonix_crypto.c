#include "zonix_crypto.h"
#include "minilib.h"
#include <limits.h>
#include <mbedtls/platform_util.h>

void mbedtls_platform_zeroize(void* buffer, size_t size) {
    volatile unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
        bytes[i] = 0;
}

int zonix_crypto_random(void* output, size_t size) {
    if ((!output && size != 0) || size > LONG_MAX)
        return SYS_ERROR_INVALID;

    unsigned char* bytes = output;
    size_t done = 0;
    while (done < size) {
        long count = sys_getrandom(bytes + done, (long)(size - done), 0);
        if (count <= 0) {
            mbedtls_platform_zeroize(bytes, done);
            return count < 0 ? (int)count : SYS_ERROR_IO;
        }
        done += (size_t)count;
    }
    return 0;
}
