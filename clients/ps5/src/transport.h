/*
 * Byte-stream transport (TCP, optionally TLS). This is the only module that touches sockets or
 * the TLS library, so porting the client to another platform or TLS backend means providing
 * another implementation of these functions.
 */
#ifndef AKENO_TRANSPORT_H
#define AKENO_TRANSPORT_H

#include <stddef.h>

#include "akeno/akeno.h"
#include "url.h"

typedef struct akeno_conn akeno_conn;

akeno_status akeno_transport_init(void);
void akeno_transport_cleanup(void);

/* Opens a connection; TLS connections verify the certificate chain and the host name. */
akeno_status akeno_conn_open(const akeno_url *url, const char *ca_file, int timeout_ms, akeno_conn **out, char *err,
                             size_t err_len);
/* Reads up to len bytes. Returns >0 bytes, 0 on orderly close, or a negative akeno_status. */
long akeno_conn_read(akeno_conn *conn, void *buf, size_t len, int timeout_ms);
akeno_status akeno_conn_write_all(akeno_conn *conn, const void *buf, size_t len, int timeout_ms);
/* Returns 1 when data can be read, 0 on timeout, or a negative akeno_status. */
int akeno_conn_wait_readable(akeno_conn *conn, int timeout_ms);
void akeno_conn_close(akeno_conn *conn);

/* Negotiated protocol and cipher of a TLS connection ("plain TCP" otherwise), for diagnostics. */
void akeno_conn_describe(const akeno_conn *conn, char *out, size_t out_len);

typedef struct {
    const char *backend;       /* e.g. "OpenSSL 3.0.13" or "mbedTLS 3.6.7" */
    const char *trust_store;   /* Where trusted roots come from when no CA file is configured. */
    const char *entropy;       /* Random number source used for TLS and WebSocket masking. */
    int trusted_roots;         /* Roots loaded into the cached trust store, -1 if not applicable. */
    int skipped_roots;         /* Roots the TLS library could not parse (ignored), -1 if n/a. */
} akeno_transport_info;

void akeno_transport_get_info(akeno_transport_info *out);

/* Monotonic clock in milliseconds. */
long long akeno_now_ms(void);
void akeno_sleep_ms(int ms);

#endif
