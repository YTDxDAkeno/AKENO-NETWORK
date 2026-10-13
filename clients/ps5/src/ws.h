/* WebSocket client (RFC 6455): text messages only, bounded message size. */
#ifndef AKENO_WS_H
#define AKENO_WS_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"
#include "transport.h"
#include "url.h"

#define AKENO_WS_MAX_MESSAGE ((size_t)1 << 20)

typedef struct {
    int fin;
    int opcode;
    int masked;
    uint64_t payload_len;
    size_t header_len;
    uint8_t mask[4];
} akeno_ws_frame;

/* Returns 1 when the header is complete, 0 when more bytes are needed, -1 on protocol error. */
int akeno_ws_parse_header(const uint8_t *data, size_t len, akeno_ws_frame *out);
/* Appends a masked client frame to out. */
int akeno_ws_encode(akeno_buf *out, int opcode, const void *payload, size_t len, const uint8_t mask[4]);
/* Sec-WebSocket-Accept value for a Sec-WebSocket-Key (29 bytes incl. NUL). */
void akeno_ws_accept_for_key(const char *key, char out[29]);

typedef struct {
    akeno_conn *conn;
    akeno_buf in;
    akeno_buf message;
    int in_message;
    int close_code;          /* Close code received from the server, 0 if none. */
    int closed;
    int io_timeout_ms;
    long long last_rx_ms;    /* Last time any frame arrived (liveness). */
} akeno_ws;

void akeno_ws_init(akeno_ws *ws, akeno_conn *conn, int io_timeout_ms);
void akeno_ws_free(akeno_ws *ws);

/*
 * Performs the opening handshake on an open connection. extra_headers (may be NULL) is added
 * verbatim and must end with CRLF. *http_status receives the HTTP status of the response.
 */
akeno_status akeno_ws_handshake(akeno_ws *ws, const akeno_url *url, const char *path, const char *extra_headers,
                                int timeout_ms, int *http_status);

/*
 * Waits up to timeout_ms for the next complete text message. Control frames are handled
 * internally. Returns AKENO_OK with text and len pointing into ws->message, AKENO_ERR_TIMEOUT when
 * nothing complete arrived, AKENO_ERR_NETWORK when the connection closed (see close_code), or
 * AKENO_ERR_PROTOCOL / AKENO_ERR_TOO_LARGE on violations.
 */
akeno_status akeno_ws_read_message(akeno_ws *ws, int timeout_ms, const char **text, size_t *len);
akeno_status akeno_ws_send_text(akeno_ws *ws, const char *text, size_t len);
akeno_status akeno_ws_send_close(akeno_ws *ws, int code);

#endif
