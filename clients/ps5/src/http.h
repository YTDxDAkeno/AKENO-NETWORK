/* Minimal HTTP/1.1 client: one request per connection, bounded responses. */
#ifndef AKENO_HTTP_H
#define AKENO_HTTP_H

#include <stddef.h>

#include "buf.h"
#include "transport.h"

#define AKENO_HTTP_MAX_HEAD 16384

typedef struct {
    int status;
    long long content_length; /* -1 when absent */
    int chunked;
    char upgrade[32];
    char connection[64];
    char ws_accept[64];
    char content_type[96];
    size_t head_len;          /* Bytes of the status line and headers including the blank line. */
} akeno_http_head;

/* Returns 1 when the head is complete, 0 when more data is needed, -1 when malformed. */
int akeno_http_parse_head(const char *data, size_t len, akeno_http_head *out);

/*
 * Decodes a complete chunked body. Returns 1 when the terminating chunk was seen (consumed set),
 * 0 when more data is needed, -1 when malformed or larger than out's limit.
 */
int akeno_http_decode_chunked(const char *data, size_t len, akeno_buf *out, size_t *consumed);

/*
 * Sends request (head + optional body) and reads the response. body_out receives the decoded
 * body (bounded by its limit). For "101 Switching Protocols" the head is returned and any bytes
 * received after it are left in body_out for the WebSocket layer.
 */
akeno_status akeno_http_exchange(akeno_conn *conn, const akeno_buf *request, int timeout_ms, akeno_http_head *head,
                                 akeno_buf *body_out);

#endif
