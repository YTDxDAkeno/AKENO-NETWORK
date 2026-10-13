#include "http.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_value(char *dst, size_t dst_len, const char *src, size_t len) {
    while (len > 0 && (*src == ' ' || *src == '\t')) { src++; len--; }
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t')) len--;
    if (len >= dst_len) len = dst_len - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

int akeno_http_parse_head(const char *data, size_t len, akeno_http_head *out) {
    memset(out, 0, sizeof(*out));
    out->content_length = -1;
    const char *end = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            end = data + i;
            break;
        }
    }
    if (!end) return len > AKENO_HTTP_MAX_HEAD ? -1 : 0;
    if ((size_t)(end - data) > AKENO_HTTP_MAX_HEAD) return -1;
    out->head_len = (size_t)(end - data) + 4;

    /* Status line: HTTP/1.x SP 3DIGIT SP reason */
    const char *line_end = memchr(data, '\r', (size_t)(end - data) + 1);
    if (!line_end || line_end - data < 12 || strncmp(data, "HTTP/1.", 7) != 0 || data[8] != ' ') return -1;
    if (!isdigit((unsigned char)data[9]) || !isdigit((unsigned char)data[10]) || !isdigit((unsigned char)data[11])) return -1;
    out->status = (data[9] - '0') * 100 + (data[10] - '0') * 10 + (data[11] - '0');
    if (out->status < 100 || out->status > 599) return -1;

    const char *p = line_end + 2;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\r') eol++;
        const char *colon = memchr(p, ':', (size_t)(eol - p));
        if (!colon || colon == p) return -1;
        size_t name_len = (size_t)(colon - p);
        const char *value = colon + 1;
        size_t value_len = (size_t)(eol - value);
        if (name_len == 14 && strncasecmp(p, "content-length", 14) == 0) {
            char tmp[24];
            copy_value(tmp, sizeof(tmp), value, value_len);
            char *num_end = NULL;
            long long n = strtoll(tmp, &num_end, 10);
            if (tmp[0] == '\0' || *num_end != '\0' || n < 0) return -1;
            if (out->content_length >= 0 && out->content_length != n) return -1; /* Conflicting lengths. */
            out->content_length = n;
        } else if (name_len == 17 && strncasecmp(p, "transfer-encoding", 17) == 0) {
            char tmp[64];
            copy_value(tmp, sizeof(tmp), value, value_len);
            if (strcasecmp(tmp, "chunked") == 0) out->chunked = 1;
            else if (strcasecmp(tmp, "identity") != 0) return -1; /* Unsupported encodings are refused. */
        } else if (name_len == 7 && strncasecmp(p, "upgrade", 7) == 0) {
            copy_value(out->upgrade, sizeof(out->upgrade), value, value_len);
        } else if (name_len == 10 && strncasecmp(p, "connection", 10) == 0) {
            copy_value(out->connection, sizeof(out->connection), value, value_len);
        } else if (name_len == 20 && strncasecmp(p, "sec-websocket-accept", 20) == 0) {
            copy_value(out->ws_accept, sizeof(out->ws_accept), value, value_len);
        } else if (name_len == 12 && strncasecmp(p, "content-type", 12) == 0) {
            copy_value(out->content_type, sizeof(out->content_type), value, value_len);
        }
        p = eol + 2;
    }
    if (out->chunked) out->content_length = -1; /* RFC 9112: chunked wins over Content-Length. */
    return 1;
}

int akeno_http_decode_chunked(const char *data, size_t len, akeno_buf *out, size_t *consumed) {
    size_t pos = 0;
    akeno_buf_reset(out);
    for (;;) {
        size_t line_start = pos;
        while (pos + 1 < len && !(data[pos] == '\r' && data[pos + 1] == '\n')) pos++;
        if (pos + 1 >= len) return 0;
        size_t size = 0;
        size_t digits = 0;
        for (size_t i = line_start; i < pos && data[i] != ';'; i++) {
            int c = (unsigned char)data[i];
            int v = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (v < 0 || digits >= 8) return -1;
            size = size * 16 + (size_t)v;
            digits++;
        }
        if (digits == 0) return -1;
        pos += 2;
        if (size == 0) {
            /* Skip optional trailers until the blank line. */
            for (;;) {
                if (pos + 1 >= len) return 0;
                if (data[pos] == '\r' && data[pos + 1] == '\n') {
                    *consumed = pos + 2;
                    return 1;
                }
                while (pos + 1 < len && !(data[pos] == '\r' && data[pos + 1] == '\n')) pos++;
                if (pos + 1 >= len) return 0;
                pos += 2;
            }
        }
        if (len - pos < size + 2) return 0;
        if (akeno_buf_append(out, data + pos, size) != 0) return -1;
        pos += size;
        if (data[pos] != '\r' || data[pos + 1] != '\n') return -1;
        pos += 2;
    }
}

akeno_status akeno_http_exchange(akeno_conn *conn, const akeno_buf *request, int timeout_ms, akeno_http_head *head,
                                 akeno_buf *body_out) {
    long long deadline = akeno_now_ms() + timeout_ms;
    akeno_status status = akeno_conn_write_all(conn, request->data, request->len, timeout_ms);
    if (status != AKENO_OK) return status;

    akeno_buf raw;
    akeno_buf_init(&raw, body_out->limit + AKENO_HTTP_MAX_HEAD + 64);
    char chunk[8192];
    int have_head = 0;
    akeno_buf_reset(body_out);
    for (;;) {
        if (have_head) {
            size_t body_len = raw.len - head->head_len;
            if (head->status == 101) break;
            if (head->status == 204 || head->status == 304) break;
            if (head->content_length >= 0 && (long long)body_len >= head->content_length) break;
            if (head->chunked) {
                size_t consumed = 0;
                int rc = akeno_http_decode_chunked(raw.data + head->head_len, body_len, body_out, &consumed);
                if (rc < 0) { status = AKENO_ERR_PROTOCOL; goto done; }
                if (rc > 0) { status = AKENO_OK; goto done; }
            }
        }
        long long left = deadline - akeno_now_ms();
        if (left <= 0) { status = AKENO_ERR_TIMEOUT; goto done; }
        long n = akeno_conn_read(conn, chunk, sizeof(chunk), (int)left);
        if (n < 0) { status = (akeno_status)(-n); goto done; }
        if (n == 0) {
            if (!have_head) { status = AKENO_ERR_PROTOCOL; goto done; }
            if (head->content_length >= 0 || head->chunked) { status = AKENO_ERR_PROTOCOL; goto done; } /* Truncated. */
            break; /* Body delimited by connection close. */
        }
        if (akeno_buf_append(&raw, chunk, (size_t)n) != 0) { status = AKENO_ERR_TOO_LARGE; goto done; }
        if (!have_head) {
            int rc = akeno_http_parse_head(raw.data, raw.len, head);
            if (rc < 0) { status = AKENO_ERR_PROTOCOL; goto done; }
            if (rc > 0) {
                have_head = 1;
                if (head->content_length > (long long)body_out->limit) { status = AKENO_ERR_TOO_LARGE; goto done; }
            }
        }
    }
    {
        size_t body_len = raw.len - head->head_len;
        if (head->content_length >= 0 && head->status != 101 && (long long)body_len > head->content_length)
            body_len = (size_t)head->content_length;
        if (head->status == 204 || head->status == 304) body_len = 0;
        if (akeno_buf_append(body_out, raw.data + head->head_len, body_len) != 0) { status = AKENO_ERR_TOO_LARGE; goto done; }
    }
    status = AKENO_OK;
    goto done;

done:
    akeno_buf_free(&raw);
    return status;
}
