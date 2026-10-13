#ifndef AKENO_URL_H
#define AKENO_URL_H

typedef struct {
    int tls;            /* 1 for https/wss */
    char host[256];
    int port;
    char base_path[256]; /* Path prefix without trailing slash ("" for the root). */
} akeno_url;

/*
 * Parses an http(s) base URL such as https://example.org or http://127.0.0.1:3000.
 * Rejects user info, query strings and fragments. Returns 0 on success.
 */
int akeno_url_parse(const char *text, akeno_url *out);
/* True for 127.0.0.0/8, ::1 and "localhost". Plain HTTP is only allowed for loopback hosts. */
int akeno_url_is_loopback(const akeno_url *url);

#endif
