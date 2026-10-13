/* Bounded, growable byte buffer. All growth is capped so a hostile server cannot exhaust memory. */
#ifndef AKENO_BUF_H
#define AKENO_BUF_H

#include <stddef.h>

typedef struct {
    char *data;     /* Always NUL-terminated when data != NULL. */
    size_t len;
    size_t cap;
    size_t limit;   /* Maximum len; 0 means AKENO_BUF_DEFAULT_LIMIT. */
} akeno_buf;

#define AKENO_BUF_DEFAULT_LIMIT ((size_t)1 << 20) /* 1 MiB */

void akeno_buf_init(akeno_buf *b, size_t limit);
void akeno_buf_free(akeno_buf *b);
void akeno_buf_reset(akeno_buf *b);
/* Returns 0 on success, -1 if the limit would be exceeded or memory is exhausted. */
int akeno_buf_append(akeno_buf *b, const void *data, size_t len);
int akeno_buf_append_str(akeno_buf *b, const char *s);
int akeno_buf_printf(akeno_buf *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
/* Appends s as a JSON string literal (with quotes), escaping as required by RFC 8259. */
int akeno_buf_append_json_string(akeno_buf *b, const char *s);
/* Removes the first n bytes. */
void akeno_buf_consume(akeno_buf *b, size_t n);

#endif
