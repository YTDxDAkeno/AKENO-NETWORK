#include "ws.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "crypto_util.h"
#include "http.h"

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

enum { OP_CONT = 0x0, OP_TEXT = 0x1, OP_BINARY = 0x2, OP_CLOSE = 0x8, OP_PING = 0x9, OP_PONG = 0xA };

int akeno_ws_parse_header(const uint8_t *data, size_t len, akeno_ws_frame *out) {
    memset(out, 0, sizeof(*out));
    if (len < 2) return 0;
    if (data[0] & 0x70) return -1; /* RSV bits: no extensions were negotiated. */
    out->fin = (data[0] & 0x80) != 0;
    out->opcode = data[0] & 0x0F;
    out->masked = (data[1] & 0x80) != 0;
    uint64_t plen = data[1] & 0x7F;
    size_t pos = 2;
    if (plen == 126) {
        if (len < 4) return 0;
        plen = (uint64_t)data[2] << 8 | data[3];
        if (plen < 126) return -1; /* Non-minimal length encoding. */
        pos = 4;
    } else if (plen == 127) {
        if (len < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = plen << 8 | data[2 + i];
        if (plen >> 63 || plen <= 0xFFFF) return -1;
        pos = 10;
    }
    if (out->masked) {
        if (len < pos + 4) return 0;
        memcpy(out->mask, data + pos, 4);
        pos += 4;
    }
    int control = out->opcode >= 0x8;
    if (control && (!out->fin || plen > 125)) return -1;
    if (!(out->opcode <= OP_BINARY || out->opcode == OP_CLOSE || out->opcode == OP_PING || out->opcode == OP_PONG)) return -1;
    out->payload_len = plen;
    out->header_len = pos;
    return 1;
}

int akeno_ws_encode(akeno_buf *out, int opcode, const void *payload, size_t len, const uint8_t mask[4]) {
    uint8_t header[14];
    size_t pos = 0;
    header[pos++] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (len < 126) {
        header[pos++] = (uint8_t)(0x80 | len);
    } else if (len <= 0xFFFF) {
        header[pos++] = 0x80 | 126;
        header[pos++] = (uint8_t)(len >> 8);
        header[pos++] = (uint8_t)len;
    } else {
        header[pos++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) header[pos++] = (uint8_t)((uint64_t)len >> (8 * i));
    }
    memcpy(header + pos, mask, 4);
    pos += 4;
    if (akeno_buf_append(out, header, pos) != 0) return -1;
    size_t start = out->len;
    if (akeno_buf_append(out, payload, len) != 0) return -1;
    for (size_t i = 0; i < len; i++) out->data[start + i] ^= (char)mask[i % 4];
    return 0;
}

void akeno_ws_accept_for_key(const char *key, char out[29]) {
    char joined[128];
    snprintf(joined, sizeof(joined), "%s%s", key, WS_GUID);
    uint8_t digest[20];
    akeno_sha1(joined, strlen(joined), digest);
    akeno_base64_encode(digest, sizeof(digest), out, 29);
}

void akeno_ws_init(akeno_ws *ws, akeno_conn *conn, int io_timeout_ms) {
    memset(ws, 0, sizeof(*ws));
    ws->conn = conn;
    ws->io_timeout_ms = io_timeout_ms;
    akeno_buf_init(&ws->in, AKENO_WS_MAX_MESSAGE + 16);
    akeno_buf_init(&ws->message, AKENO_WS_MAX_MESSAGE);
    ws->last_rx_ms = akeno_now_ms();
}

void akeno_ws_free(akeno_ws *ws) {
    akeno_buf_free(&ws->in);
    akeno_buf_free(&ws->message);
}

static int header_has_token(const char *value, const char *token) {
    size_t n = strlen(token);
    const char *p = value;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        size_t len = strcspn(p, ",");
        size_t trimmed = len;
        while (trimmed > 0 && p[trimmed - 1] == ' ') trimmed--;
        if (trimmed == n && strncasecmp(p, token, n) == 0) return 1;
        p += len;
    }
    return 0;
}

akeno_status akeno_ws_handshake(akeno_ws *ws, const akeno_url *url, const char *path, const char *extra_headers,
                                int timeout_ms, int *http_status) {
    *http_status = 0;
    uint8_t nonce[16];
    if (akeno_random_bytes(nonce, sizeof(nonce)) != 0) return AKENO_ERR_TLS;
    char key[32];
    akeno_base64_encode(nonce, sizeof(nonce), key, sizeof(key));

    akeno_buf request;
    akeno_buf_init(&request, 8192);
    int bracket = strchr(url->host, ':') != NULL;
    int default_port = url->port == (url->tls ? 443 : 80);
    int rc = akeno_buf_printf(&request, "GET %s%s HTTP/1.1\r\nHost: %s%s%s", url->base_path, path, bracket ? "[" : "",
                              url->host, bracket ? "]" : "");
    if (rc == 0 && !default_port) rc = akeno_buf_printf(&request, ":%d", url->port);
    if (rc == 0)
        rc = akeno_buf_printf(&request,
                              "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
                              "Sec-WebSocket-Version: 13\r\nUser-Agent: akeno-device-client/" AKENO_CLIENT_VERSION "\r\n%s\r\n",
                              key, extra_headers ? extra_headers : "");
    if (rc != 0) {
        akeno_buf_free(&request);
        return AKENO_ERR_NO_MEMORY;
    }
    akeno_http_head head;
    akeno_buf leftover;
    akeno_buf_init(&leftover, 65536);
    akeno_status status = akeno_http_exchange(ws->conn, &request, timeout_ms, &head, &leftover);
    akeno_buf_free(&request);
    if (status != AKENO_OK) {
        akeno_buf_free(&leftover);
        return status;
    }
    *http_status = head.status;
    if (head.status != 101) {
        akeno_buf_free(&leftover);
        switch (head.status) {
        case 401: return AKENO_ERR_UNAUTHORIZED;
        case 403: return AKENO_ERR_FORBIDDEN;
        case 429: return AKENO_ERR_RATE_LIMITED;
        default: return head.status >= 500 ? AKENO_ERR_SERVER : AKENO_ERR_PROTOCOL;
        }
    }
    char expected[29];
    akeno_ws_accept_for_key(key, expected);
    if (strcasecmp(head.upgrade, "websocket") != 0 || !header_has_token(head.connection, "upgrade") ||
        strcmp(head.ws_accept, expected) != 0) {
        akeno_buf_free(&leftover);
        return AKENO_ERR_PROTOCOL;
    }
    akeno_buf_reset(&ws->in);
    status = akeno_buf_append(&ws->in, leftover.data ? leftover.data : "", leftover.len) == 0 ? AKENO_OK : AKENO_ERR_TOO_LARGE;
    akeno_buf_free(&leftover);
    ws->last_rx_ms = akeno_now_ms();
    return status;
}

static akeno_status send_frame(akeno_ws *ws, int opcode, const void *payload, size_t len) {
    uint8_t mask[4];
    if (akeno_random_bytes(mask, sizeof(mask)) != 0) return AKENO_ERR_TLS;
    akeno_buf frame;
    akeno_buf_init(&frame, len + 16);
    if (akeno_ws_encode(&frame, opcode, payload, len, mask) != 0) {
        akeno_buf_free(&frame);
        return AKENO_ERR_NO_MEMORY;
    }
    akeno_status status = akeno_conn_write_all(ws->conn, frame.data, frame.len, ws->io_timeout_ms);
    akeno_buf_free(&frame);
    return status;
}

akeno_status akeno_ws_send_text(akeno_ws *ws, const char *text, size_t len) {
    if (ws->closed) return AKENO_ERR_NETWORK;
    return send_frame(ws, OP_TEXT, text, len);
}

akeno_status akeno_ws_send_close(akeno_ws *ws, int code) {
    if (ws->closed) return AKENO_OK;
    uint8_t payload[2] = { (uint8_t)(code >> 8), (uint8_t)code };
    ws->closed = 1;
    return send_frame(ws, OP_CLOSE, payload, sizeof(payload));
}

akeno_status akeno_ws_read_message(akeno_ws *ws, int timeout_ms, const char **text, size_t *len) {
    long long deadline = akeno_now_ms() + timeout_ms;
    for (;;) {
        akeno_ws_frame frame;
        int rc = akeno_ws_parse_header((const uint8_t *)ws->in.data, ws->in.len, &frame);
        if (rc < 0) return AKENO_ERR_PROTOCOL;
        if (rc > 0 && frame.masked) return AKENO_ERR_PROTOCOL; /* Servers must not mask frames. */
        if (rc > 0 && frame.payload_len > AKENO_WS_MAX_MESSAGE) return AKENO_ERR_TOO_LARGE;
        if (rc > 0 && ws->in.len - frame.header_len >= frame.payload_len) {
            const char *payload = ws->in.data + frame.header_len;
            size_t plen = (size_t)frame.payload_len;
            size_t total = frame.header_len + plen;
            ws->last_rx_ms = akeno_now_ms();
            switch (frame.opcode) {
            case OP_PING: {
                char copy[125];
                memcpy(copy, payload, plen);
                akeno_buf_consume(&ws->in, total);
                akeno_status s = send_frame(ws, OP_PONG, copy, plen);
                if (s != AKENO_OK) return s;
                continue;
            }
            case OP_PONG:
                akeno_buf_consume(&ws->in, total);
                continue;
            case OP_CLOSE:
                if (plen == 1) return AKENO_ERR_PROTOCOL;
                ws->close_code = plen >= 2 ? ((uint8_t)payload[0] << 8 | (uint8_t)payload[1]) : 1005;
                akeno_buf_consume(&ws->in, total);
                if (!ws->closed) {
                    uint8_t echo[2] = { (uint8_t)(ws->close_code == 1005 ? 1000 >> 8 : ws->close_code >> 8),
                                        (uint8_t)(ws->close_code == 1005 ? 1000 & 0xFF : ws->close_code & 0xFF) };
                    ws->closed = 1;
                    send_frame(ws, OP_CLOSE, echo, sizeof(echo)); /* Best effort. */
                }
                return AKENO_ERR_NETWORK;
            case OP_BINARY:
                return AKENO_ERR_PROTOCOL; /* The AKENO protocol is text-only. */
            case OP_TEXT:
                if (ws->in_message) return AKENO_ERR_PROTOCOL;
                akeno_buf_reset(&ws->message);
                ws->in_message = 1;
                /* fall through */
            case OP_CONT:
                if (!ws->in_message) return AKENO_ERR_PROTOCOL;
                if (akeno_buf_append(&ws->message, payload, plen) != 0) return AKENO_ERR_TOO_LARGE;
                akeno_buf_consume(&ws->in, total);
                if (frame.fin) {
                    ws->in_message = 0;
                    *text = ws->message.data ? ws->message.data : "";
                    *len = ws->message.len;
                    return AKENO_OK;
                }
                continue;
            default:
                return AKENO_ERR_PROTOCOL;
            }
        }
        if (ws->closed) return AKENO_ERR_NETWORK;
        long long left = deadline - akeno_now_ms();
        if (left <= 0) return AKENO_ERR_TIMEOUT;
        int ready = akeno_conn_wait_readable(ws->conn, (int)left);
        if (ready == 0) return AKENO_ERR_TIMEOUT;
        if (ready < 0) return AKENO_ERR_NETWORK;
        char chunk[8192];
        long n = akeno_conn_read(ws->conn, chunk, sizeof(chunk), ws->io_timeout_ms);
        if (n == 0) {
            ws->closed = 1;
            if (!ws->close_code) ws->close_code = 1006;
            return AKENO_ERR_NETWORK;
        }
        if (n < 0) return n == -(long)AKENO_ERR_TIMEOUT ? AKENO_ERR_TIMEOUT : AKENO_ERR_NETWORK;
        if (akeno_buf_append(&ws->in, chunk, (size_t)n) != 0) return AKENO_ERR_TOO_LARGE;
    }
}
