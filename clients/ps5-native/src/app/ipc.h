/*
 * HTTP/1.1 client for the AKENO daemon's local interface, 127.0.0.1 only (docs/PS5_NATIVE_PHASE2.md).
 * Phase 2.1 has GET only: through this build AKENO Native reads, it never changes anything.
 * Blocking with a deadline; call from a worker thread.
 */
#ifndef AKN_IPC_H
#define AKN_IPC_H

#include <stddef.h>

typedef struct {
    int status;          /* HTTP status; 0 when there was no valid answer */
    char *body;          /* NUL-terminated (malloc'd), NULL when there is none */
    size_t len;
    int connected;       /* something accepted the connection on the port */
    int transport_error; /* connect, send or receive failed, timed out or the answer was too large */
} akn_http_reply;

/* GET path (with its query) from 127.0.0.1:port, sending X-Akeno-Native-Key when native_key is set.
 * Returns the HTTP status, or 0 on a transport error. Free the reply with akn_http_reply_free(). */
int akn_loopback_get(int port, const char *path, const char *native_key, int timeout_ms, size_t max_body, akn_http_reply *out);
void akn_http_reply_free(akn_http_reply *reply);

#endif
