/* Small, dependency-free primitives needed by the WebSocket handshake (RFC 6455). */
#ifndef AKENO_CRYPTO_UTIL_H
#define AKENO_CRYPTO_UTIL_H

#include <stddef.h>
#include <stdint.h>

void akeno_sha1(const void *data, size_t len, uint8_t out[20]);
/* Writes NUL-terminated standard Base64 into out; returns -1 if out_len is too small. */
int akeno_base64_encode(const uint8_t *data, size_t len, char *out, size_t out_len);
/* Cryptographically secure random bytes from the TLS backend. Returns 0 on success. */
int akeno_random_bytes(void *out, size_t len);

#endif
