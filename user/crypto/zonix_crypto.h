#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fill the writable buffer or return a negative Zonix error. On failure,
// bytes already returned by the kernel are erased.
int zonix_crypto_random(void* output, size_t size);

#ifdef __cplusplus
}
#endif
