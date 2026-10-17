#define _POSIX_C_SOURCE 200809L

#include "ipc.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define LOOPBACK_ADDR 0x7F000001u /* 127.0.0.1 (INADDR_LOOPBACK is BSD-only in strict POSIX mode) */
#define HEAD_MAX 8192

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_fd(int fd, short events, long long deadline) {
    for (;;) {
        long long left = deadline - now_ms();
        if (left <= 0) return 0;
        struct pollfd p = { fd, events, 0 };
        int rc = poll(&p, 1, (int)left);
        if (rc < 0 && errno == EINTR) continue;
        return rc;
    }
}

/* Value of header `name` in the head block (case-insensitive name), or NULL. */
static const char *header(const char *head, const char *name) {
    size_t nl = strlen(name);
    for (const char *line = strstr(head, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        const char *h = line + 2;
        if (strncasecmp(h, name, nl) != 0 || h[nl] != ':') continue;
        h += nl + 1;
        while (*h == ' ' || *h == '\t') h++;
        return h;
    }
    return NULL;
}

/* Decodes a chunked body in place; returns its length or -1 when it is malformed or incomplete. */
static long dechunk(char *buf, size_t len) {
    size_t in = 0, out = 0;
    for (;;) {
        char *end;
        if (in >= len) return -1;
        unsigned long size = strtoul(buf + in, &end, 16);
        if (end == buf + in || (size_t)(end - buf) + 2 > len || strncmp(end, "\r\n", 2) != 0) return -1;
        in = (size_t)(end - buf) + 2;
        if (size == 0) return (long)out;
        if (size > len - in || len - in - size < 2 || strncmp(buf + in + size, "\r\n", 2) != 0) return -1;
        memmove(buf + out, buf + in, size);
        out += size;
        in += size + 2;
    }
}

void akn_http_reply_free(akn_http_reply *reply) {
    free(reply->body);
    memset(reply, 0, sizeof(*reply));
}

int akn_loopback_get(int port, const char *path, const char *native_key, int timeout_ms, size_t max_body, akn_http_reply *out) {
    memset(out, 0, sizeof(*out));
    out->transport_error = 1;
    long long deadline = now_ms() + timeout_ms;
    char request[1024];
    int n = snprintf(request, sizeof(request),
                     "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nAccept: application/json\r\n%s%s%sConnection: close\r\n\r\n", path, port,
                     native_key ? "X-Akeno-Native-Key: " : "", native_key ? native_key : "", native_key ? "\r\n" : "");
    if (n <= 0 || (size_t)n >= sizeof(request) || path[0] != '/') return 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(LOOPBACK_ADDR);
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0 && errno == EINPROGRESS) {
        int err = 0;
        socklen_t elen = sizeof(err);
        if (wait_fd(fd, POLLOUT, deadline) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) rc = 0;
    }
    out->connected = rc == 0;
    size_t sent = 0;
    while (rc == 0 && sent < (size_t)n) {
        if (wait_fd(fd, POLLOUT, deadline) <= 0) break;
        ssize_t w = send(fd, request + sent, (size_t)n - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    memset(request, 0, sizeof(request)); /* the key */
    size_t cap = HEAD_MAX + max_body + 1, got = 0;
    char *buf = sent == (size_t)n ? malloc(cap) : NULL;
    int complete = 0;
    while (buf) {
        if (got + 1 >= cap) break; /* too large */
        if (wait_fd(fd, POLLIN, deadline) <= 0) break;
        ssize_t r = recv(fd, buf + got, cap - 1 - got, 0);
        if (r < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (r < 0) break;
        if (r == 0) {
            complete = 1;
            break;
        }
        got += (size_t)r;
    }
    close(fd);
    if (!buf) return 0;
    buf[got] = '\0';
    char *body = strstr(buf, "\r\n\r\n");
    int status = 0;
    if (!complete || !body || (size_t)(body - buf) > HEAD_MAX || sscanf(buf, "HTTP/1.%*d %d", &status) != 1 || status < 100 || status > 599) {
        free(buf);
        return 0;
    }
    *body = '\0';
    body += 4;
    size_t len = got - (size_t)(body - buf);
    const char *te = header(buf, "transfer-encoding");
    const char *cl = header(buf, "content-length");
    if (te && strncasecmp(te, "chunked", 7) == 0) {
        long d = dechunk(body, len);
        if (d < 0) {
            free(buf);
            return 0;
        }
        len = (size_t)d;
    } else if (cl) {
        char *end;
        unsigned long v = strtoul(cl, &end, 10);
        if (end == cl || v != len) { /* cut short */
            free(buf);
            return 0;
        }
    }
    out->body = malloc(len + 1);
    if (!out->body) {
        free(buf);
        return 0;
    }
    memcpy(out->body, body, len);
    out->body[len] = '\0';
    out->len = len;
    out->status = status;
    out->transport_error = 0;
    free(buf);
    return status;
}
