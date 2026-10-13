#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void akeno_buf_init(akeno_buf *b, size_t limit) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->limit = limit ? limit : AKENO_BUF_DEFAULT_LIMIT;
}

void akeno_buf_free(akeno_buf *b) {
    if (b->data) {
        /* Buffers may hold tokens: wipe before releasing. */
        memset(b->data, 0, b->cap);
        free(b->data);
    }
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void akeno_buf_reset(akeno_buf *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

static int reserve(akeno_buf *b, size_t extra) {
    if (extra > b->limit || b->len > b->limit - extra) return -1;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > ((size_t)-1) / 2) return -1;
        cap *= 2;
    }
    char *next = malloc(cap);
    if (!next) return -1;
    if (b->data) {
        memcpy(next, b->data, b->len);
        memset(b->data, 0, b->cap);
        free(b->data);
    }
    b->data = next;
    b->cap = cap;
    b->data[b->len] = '\0';
    return 0;
}

int akeno_buf_append(akeno_buf *b, const void *data, size_t len) {
    if (reserve(b, len) != 0) return -1;
    if (len) memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

int akeno_buf_append_str(akeno_buf *b, const char *s) {
    return akeno_buf_append(b, s, strlen(s));
}

int akeno_buf_printf(akeno_buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0 || reserve(b, (size_t)n) != 0) {
        va_end(ap);
        return -1;
    }
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}

int akeno_buf_append_json_string(akeno_buf *b, const char *s) {
    static const char hex[] = "0123456789abcdef";
    if (akeno_buf_append(b, "\"", 1) != 0) return -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[7];
        size_t n = 0;
        switch (*p) {
        case '"': esc[0] = '\\'; esc[1] = '"'; n = 2; break;
        case '\\': esc[0] = '\\'; esc[1] = '\\'; n = 2; break;
        case '\n': esc[0] = '\\'; esc[1] = 'n'; n = 2; break;
        case '\r': esc[0] = '\\'; esc[1] = 'r'; n = 2; break;
        case '\t': esc[0] = '\\'; esc[1] = 't'; n = 2; break;
        default:
            if (*p < 0x20) {
                esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                esc[4] = hex[*p >> 4]; esc[5] = hex[*p & 0xF];
                n = 6;
            }
        }
        if (n) {
            if (akeno_buf_append(b, esc, n) != 0) return -1;
        } else if (akeno_buf_append(b, p, 1) != 0) {
            return -1;
        }
    }
    return akeno_buf_append(b, "\"", 1);
}

void akeno_buf_consume(akeno_buf *b, size_t n) {
    if (n >= b->len) {
        akeno_buf_reset(b);
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = '\0';
}
