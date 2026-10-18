#include "redact.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    char *dst;
    size_t len, used;
} out_buf;

static void put(out_buf *o, const char *s, size_t n) {
    if (o->len == 0) return;
    size_t room = o->len - 1 - o->used;
    if (n > room) n = room;
    memcpy(o->dst + o->used, s, n);
    o->used += n;
    o->dst[o->used] = '\0';
}

static void puts_(out_buf *o, const char *s) { put(o, s, strlen(s)); }

static int token_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '-'; }

static int ci_prefix(const char *s, const char *prefix) {
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    return 1;
}

static int is_loopback_host(const char *h, size_t n) {
    return (n == 9 && strncmp(h, "localhost", 9) == 0) || (n == 9 && strncmp(h, "127.0.0.1", 9) == 0) ||
           (n == 5 && strncmp(h, "[::1]", 5) == 0);
}

char *akn_redact(char *dst, size_t dst_len, const char *src) {
    out_buf o = { dst, dst_len, 0 };
    if (dst_len) dst[0] = '\0';
    if (!src) return dst;
    const char *p = src;
    while (*p) {
        /* AKENO secrets: device tokens (akd_), pairing device codes (akp_) and any other ak?_ value. */
        if (p[0] == 'a' && p[1] == 'k' && islower((unsigned char)p[2]) && p[3] == '_' && (p == src || !token_char(p[-1]))) {
            const char *q = p + 4;
            size_t n = 0;
            while (token_char(q[n])) n++;
            if (n >= 6) {
                put(&o, p, 4);
                puts_(&o, "[redacted]");
                p = q + n;
                continue;
            }
        }
        /* Authorization header values. */
        if (ci_prefix(p, "bearer ")) {
            put(&o, p, 7);
            const char *q = p + 7;
            while (*q == ' ') q++;
            size_t n = 0;
            while (q[n] && !isspace((unsigned char)q[n]) && q[n] != '"' && q[n] != ',') n++;
            if (n) {
                puts_(&o, "[redacted]");
                p = q + n;
            } else {
                p += 7;
            }
            continue;
        }
        if (ci_prefix(p, "x-akeno-")) {
            const char *colon = strchr(p, ':');
            const char *eol = strpbrk(p, "\r\n");
            if (colon && (!eol || colon < eol) && colon - p < 40) {
                put(&o, p, (size_t)(colon - p + 1));
                puts_(&o, " [redacted]");
                p = eol ? eol : p + strlen(p);
                continue;
            }
        }
        /* Long hex runs (keys, secrets). */
        if (isxdigit((unsigned char)*p) && (p == src || !isalnum((unsigned char)p[-1]))) {
            size_t n = 0;
            while (isxdigit((unsigned char)p[n])) n++;
            if (n >= 32 && !isalnum((unsigned char)p[n])) {
                puts_(&o, "[redacted-hex]");
                p += n;
                continue;
            }
        }
        /* URL hosts. */
        if (ci_prefix(p, "https://") || ci_prefix(p, "http://") || ci_prefix(p, "wss://") || ci_prefix(p, "ws://")) {
            const char *host = strstr(p, "://") + 3;
            size_t n = 0;
            while (host[n] && host[n] != '/' && host[n] != ':' && host[n] != '?' && host[n] != '#' &&
                   !isspace((unsigned char)host[n]) && host[n] != '"')
                n++;
            put(&o, p, (size_t)(host - p));
            if (is_loopback_host(host, n)) put(&o, host, n);
            else puts_(&o, "[server]");
            p = host + n;
            /* A port number reveals little but is private configuration too. */
            if (*p == ':' && !is_loopback_host(host, n)) {
                p++;
                while (isdigit((unsigned char)*p)) p++;
            }
            continue;
        }
        put(&o, p, 1);
        p++;
    }
    return dst;
}

const char *akn_describe_server_url(const char *url) {
    if (!url || !url[0]) return "not configured";
    if (strncmp(url, "https://", 8) == 0 && url[8]) return "https (host hidden)";
    if (strncmp(url, "http://127.0.0.1", 16) == 0 || strncmp(url, "http://localhost", 16) == 0) return "http to loopback";
    return "invalid";
}
