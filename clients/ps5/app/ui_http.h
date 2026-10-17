/*
 * HTTP/1.1 request parsing for the console app's local UI server. The server only listens on
 * 127.0.0.1, but any local process or web page may still connect, so parsing is strict and
 * bounded: one request per connection, no chunked request bodies, small fixed-size fields.
 */
#ifndef AKENO_UI_HTTP_H
#define AKENO_UI_HTTP_H

#include <stddef.h>

#define UI_HTTP_MAX_HEAD 8192
#define UI_HTTP_MAX_BODY (64 * 1024)

typedef struct ui_request_s {
    char method[8];
    char path[256];          /* Request target up to '?' (percent-encoding is not decoded). */
    char query[256];         /* After '?', without it; "" if none. */
    char host[96];
    char origin[128];        /* "" when absent. */
    char token[96];          /* X-Akeno-Token */
    char instance_key[96];   /* X-Akeno-Instance-Key */
#ifdef AKENO_NATIVE_IPC
    char native_key[96];     /* X-Akeno-Native-Key (AKENO Native, native-IPC build only) */
#endif
    char content_type[64];
    long long content_length; /* -1 when absent. */
    size_t head_len;
} ui_request;

enum { UI_PARSE_INCOMPLETE = 0, UI_PARSE_OK = 1, UI_PARSE_BAD = -1, UI_PARSE_TOO_LARGE = -2, UI_PARSE_UNSUPPORTED = -3 };

/* Parses the request head in data[0..len). Returns one of the UI_PARSE_* values. */
int ui_http_parse_request(const char *data, size_t len, ui_request *out);

/* Copies the value of query parameter name (no decoding; [A-Za-z0-9_-] only) into out. */
int ui_http_query_param(const char *query, const char *name, char *out, size_t out_len);

/* True when host is exactly "127.0.0.1:<port>" or "localhost:<port>" (DNS rebinding defence). */
int ui_http_host_allowed(const char *host, int port);

/* True when origin is absent or is the UI's own origin. */
int ui_http_origin_allowed(const char *origin, int port);

/* Constant-time comparison of two NUL-terminated secrets. */
int ui_http_secret_equal(const char *a, const char *b);

#endif
