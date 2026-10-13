#include "ui_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Copies a trimmed header value; returns -1 if it does not fit (callers reject the request). */
static int copy_field(char *dst, size_t dst_len, const char *src, size_t len) {
    while (len > 0 && (*src == ' ' || *src == '\t')) { src++; len--; }
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t')) len--;
    if (len >= dst_len) return -1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 0;
}

static int visible(unsigned char c) { return c > 0x20 && c < 0x7F; }

int ui_http_parse_request(const char *data, size_t len, ui_request *out) {
    memset(out, 0, sizeof(*out));
    out->content_length = -1;
    const char *end = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            end = data + i;
            break;
        }
    }
    if (!end) return len >= UI_HTTP_MAX_HEAD ? UI_PARSE_TOO_LARGE : UI_PARSE_INCOMPLETE;
    if ((size_t)(end - data) + 4 > UI_HTTP_MAX_HEAD) return UI_PARSE_TOO_LARGE;
    out->head_len = (size_t)(end - data) + 4;

    /* Request line: METHOD SP target SP HTTP/1.x */
    const char *line_end = memchr(data, '\r', (size_t)(end - data) + 1);
    if (!line_end) return UI_PARSE_BAD;
    const char *sp1 = memchr(data, ' ', (size_t)(line_end - data));
    if (!sp1 || sp1 == data || (size_t)(sp1 - data) >= sizeof(out->method)) return UI_PARSE_BAD;
    for (const char *p = data; p < sp1; p++)
        if (*p < 'A' || *p > 'Z') return UI_PARSE_BAD;
    memcpy(out->method, data, (size_t)(sp1 - data));
    const char *target = sp1 + 1;
    const char *sp2 = memchr(target, ' ', (size_t)(line_end - target));
    if (!sp2 || sp2 == target || *target != '/') return UI_PARSE_BAD;
    if ((size_t)(line_end - sp2 - 1) != 8 || strncmp(sp2 + 1, "HTTP/1.", 7) != 0 || (sp2[8] != '0' && sp2[8] != '1'))
        return UI_PARSE_BAD;
    for (const char *p = target; p < sp2; p++)
        if (!visible((unsigned char)*p)) return UI_PARSE_BAD;
    const char *q = memchr(target, '?', (size_t)(sp2 - target));
    const char *path_end = q ? q : sp2;
    if ((size_t)(path_end - target) >= sizeof(out->path)) return UI_PARSE_TOO_LARGE;
    memcpy(out->path, target, (size_t)(path_end - target));
    if (q) {
        if ((size_t)(sp2 - q - 1) >= sizeof(out->query)) return UI_PARSE_TOO_LARGE;
        memcpy(out->query, q + 1, (size_t)(sp2 - q - 1));
    }

    int seen_host = 0;
    const char *p = line_end + 2;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\r') eol++;
        if (eol < end && eol[1] != '\n') return UI_PARSE_BAD;
        const char *colon = memchr(p, ':', (size_t)(eol - p));
        if (!colon || colon == p) return UI_PARSE_BAD;
        for (const char *c = p; c < colon; c++)
            if (!visible((unsigned char)*c)) return UI_PARSE_BAD; /* Also rejects obsolete line folding. */
        for (const char *c = colon + 1; c < eol; c++)
            if ((unsigned char)*c < 0x20 && *c != '\t') return UI_PARSE_BAD;
        size_t name_len = (size_t)(colon - p);
        const char *value = colon + 1;
        size_t value_len = (size_t)(eol - value);
#define IS(name) (name_len == sizeof(name) - 1 && strncasecmp(p, name, name_len) == 0)
        int rc = 0;
        if (IS("host")) {
            if (seen_host++) return UI_PARSE_BAD;
            rc = copy_field(out->host, sizeof(out->host), value, value_len);
        } else if (IS("origin")) {
            rc = copy_field(out->origin, sizeof(out->origin), value, value_len);
        } else if (IS("x-akeno-token")) {
            rc = copy_field(out->token, sizeof(out->token), value, value_len);
        } else if (IS("x-akeno-instance-key")) {
            rc = copy_field(out->instance_key, sizeof(out->instance_key), value, value_len);
        } else if (IS("content-type")) {
            rc = copy_field(out->content_type, sizeof(out->content_type), value, value_len);
        } else if (IS("content-length")) {
            char tmp[24];
            if (copy_field(tmp, sizeof(tmp), value, value_len) != 0 || tmp[0] == '\0') return UI_PARSE_BAD;
            for (const char *d = tmp; *d; d++)
                if (*d < '0' || *d > '9') return UI_PARSE_BAD;
            long long n = strtoll(tmp, NULL, 10);
            if (out->content_length >= 0 && out->content_length != n) return UI_PARSE_BAD;
            out->content_length = n;
        } else if (IS("transfer-encoding")) {
            return UI_PARSE_UNSUPPORTED;
        }
#undef IS
        if (rc != 0) return UI_PARSE_TOO_LARGE;
        p = eol + 2;
    }
    if (!seen_host) return UI_PARSE_BAD;
    if (out->content_length > UI_HTTP_MAX_BODY) return UI_PARSE_TOO_LARGE;
    return UI_PARSE_OK;
}

int ui_http_query_param(const char *query, const char *name, char *out, size_t out_len) {
    size_t name_len = strlen(name);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len > name_len && strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            const char *v = p + name_len + 1;
            size_t vlen = len - name_len - 1;
            if (vlen >= out_len) return -1;
            for (size_t i = 0; i < vlen; i++) {
                char c = v[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) return -1;
            }
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return 0;
        }
        p = amp ? amp + 1 : NULL;
    }
    return -1;
}

int ui_http_host_allowed(const char *host, int port) {
    char a[48], b[48];
    snprintf(a, sizeof(a), "127.0.0.1:%d", port);
    snprintf(b, sizeof(b), "localhost:%d", port);
    return strcmp(host, a) == 0 || strcasecmp(host, b) == 0;
}

int ui_http_origin_allowed(const char *origin, int port) {
    if (!origin[0]) return 1;
    char a[48], b[48];
    snprintf(a, sizeof(a), "http://127.0.0.1:%d", port);
    snprintf(b, sizeof(b), "http://localhost:%d", port);
    return strcmp(origin, a) == 0 || strcasecmp(origin, b) == 0;
}

int ui_http_secret_equal(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    unsigned diff = (unsigned)(la ^ lb);
    for (size_t i = 0; i < la && i < lb; i++) diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return diff == 0 && la > 0;
}
