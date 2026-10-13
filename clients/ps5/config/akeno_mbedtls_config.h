/*
 * mbedTLS configuration for the AKENO device client (selected with
 * -DMBEDTLS_CONFIG_FILE="akeno_mbedtls_config.h").
 *
 * Starts from the upstream default configuration and only removes modules the client does not
 * use (server side, mbedTLS's own sockets/timing/file helpers, persistent key storage, self
 * tests). Nothing that affects certificate or host name verification is disabled.
 *
 * Entropy comes exclusively from akeno_platform_entropy() through mbedtls_hardware_poll()
 * (see transport_mbedtls.c): getrandom() on Linux, the kernel's kern.arandom sysctl on the PS5.
 * If that source fails, TLS initialization fails; there is no weak fallback.
 */
#ifndef AKENO_MBEDTLS_CONFIG_H
#define AKENO_MBEDTLS_CONFIG_H

#include "mbedtls/mbedtls_config.h"

/* Client only. */
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_CACHE_C

/* The client brings its own sockets, clock and file handling. */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C

#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_DEBUG_C

/* Only the strong source below; no platform-specific guessing inside mbedTLS. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Connections are opened from several threads (UI, realtime, pairing). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD

#endif
