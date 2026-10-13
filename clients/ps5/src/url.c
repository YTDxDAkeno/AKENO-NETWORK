#include "url.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int akeno_url_parse(const char *text, akeno_url *out) {
    memset(out, 0, sizeof(*out));
    const char *p;
    if (strncasecmp(text, "https://", 8) == 0) {
        out->tls = 1;
        out->port = 443;
        p = text + 8;
    } else if (strncasecmp(text, "http://", 7) == 0) {
        out->tls = 0;
        out->port = 80;
        p = text + 7;
    } else {
        return -1;
    }
    if (strpbrk(text, "?#@ \t\r\n")) return -1;

    const char *host_end;
    const char *host_start = p;
    if (*p == '[') {
        host_end = strchr(p, ']');
        if (!host_end) return -1;
        host_start = p + 1;
        p = host_end + 1;
    } else {
        host_end = p + strcspn(p, ":/");
        p = host_end;
    }
    size_t host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(out->host)) return -1;
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';
    for (size_t i = 0; i < host_len; i++) {
        unsigned char c = (unsigned char)out->host[i];
        if (!(isalnum(c) || c == '.' || c == '-' || c == ':')) return -1;
        out->host[i] = (char)tolower(c);
    }

    if (*p == ':') {
        p++;
        char *end = NULL;
        long port = strtol(p, &end, 10);
        if (end == p || port < 1 || port > 65535) return -1;
        out->port = (int)port;
        p = end;
    }
    if (*p && *p != '/') return -1;
    size_t path_len = strlen(p);
    while (path_len > 0 && p[path_len - 1] == '/') path_len--;
    if (path_len >= sizeof(out->base_path)) return -1;
    memcpy(out->base_path, p, path_len);
    out->base_path[path_len] = '\0';
    return 0;
}

int akeno_url_is_loopback(const akeno_url *url) {
    return strcmp(url->host, "localhost") == 0 || strcmp(url->host, "::1") == 0 || strncmp(url->host, "127.", 4) == 0;
}
