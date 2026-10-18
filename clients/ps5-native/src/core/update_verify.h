/*
 * Cryptographic checks of the updater (mbedTLS, the same library as the TLS transport):
 * SHA-256 of buffers and files, and the manifest signature: ECDSA on P-256 over SHA-256 of the
 * manifest's exact bytes, DER-encoded, verified with a PEM public key compiled into the app.
 */
#ifndef AKN_UPDATE_VERIFY_H
#define AKN_UPDATE_VERIFY_H

#include <stddef.h>
#include <stdint.h>

void akn_sha256(const void *data, size_t len, uint8_t out[32]);

/* SHA-256 and size of a file; returns 0, or -1 when it cannot be read. */
int akn_sha256_file(const char *path, uint8_t out[32], long long *size);

/*
 * Returns 0 when sig is a valid signature of data by the P-256 key in pem (a NUL-terminated
 * "-----BEGIN PUBLIC KEY-----" block); -1 otherwise, with the reason in why. A key that is not an
 * EC P-256 key is refused.
 */
int akn_update_verify_signature(const char *pem, const void *data, size_t len, const uint8_t *sig, size_t sig_len,
                                char *why, size_t why_len);

/* The public key compiled into this build, or NULL when none is configured (installs refused). */
const char *akn_update_trusted_key(void);

#endif
