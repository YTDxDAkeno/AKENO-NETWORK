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

/* Monotonic clock in milliseconds. */
long long akeno_now_ms(void);
void akeno_sleep_ms(int ms);

#endif
