/*
 * Local HTTP server for the console UI. Listens on 127.0.0.1 only and serves one request per
 * connection, sequentially, from the main thread. Every connection has a deadline and size
 * limits, so a misbehaving local client cannot block the app for long or exhaust memory.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "app_internal.h"
#include "net_posix.h"
#include "transport.h"
#include "ui_http.h"

#define READ_DEADLINE_MS 5000
#define WRITE_DEADLINE_MS 10000
#define RESPONSE_LIMIT ((size_t)2 << 20)

int app_exit_requested(void);

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ui_server_listen(int port, int *bound_port, int *in_use) {
    *in_use = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* Never reachable from the network. */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        *in_use = errno == EADDRINUSE;
        close(fd);
        return -1;
    }
    socklen_t len = sizeof(addr);
    if (listen(fd, 16) != 0 || getsockname(fd, (struct sockaddr *)&addr, &len) != 0 || set_nonblocking(fd) != 0) {
        close(fd);
        return -1;
    }
    *bound_port = ntohs(addr.sin_port);
    return fd;
}

static int write_all(int fd, const char *data, size_t len, long long deadline) {
    while (len > 0) {
        long n = akeno_send_nosignal(fd, data, len);
        if (n > 0) { data += n; len -= (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && akeno_wait_fd(fd, POLLOUT, akeno_remaining_ms(deadline)) > 0) continue;
        return -1;
    }
    return 0;
}

static const char *reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    default: return "Error";
    }
}

static void send_response(int fd, const ui_response *r, int head_only) {
    const char *body = r->static_body ? r->static_body : r->body.data;
    size_t body_len = r->static_body ? r->static_len : r->body.len;
    if (r->status == 204) body_len = 0;
    char head[1024];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Length: %zu\r\n"
                     "%s%s%s"
                     "Cache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "X-Frame-Options: DENY\r\n"
                     "Referrer-Policy: no-referrer\r\n"
                     "Cross-Origin-Resource-Policy: same-origin\r\n"
                     "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
                     "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'\r\n"
                     "Connection: close\r\n\r\n",
                     r->status, reason(r->status), body_len, r->content_type ? "Content-Type: " : "",
                     r->content_type ? r->content_type : "", r->content_type ? "\r\n" : "");
    long long deadline = akeno_now_ms() + WRITE_DEADLINE_MS;
    if (n <= 0 || (size_t)n >= sizeof(head) || write_all(fd, head, (size_t)n, deadline) != 0) return;
    if (!head_only && body_len > 0) write_all(fd, body, body_len, deadline);
}

static void send_error(int fd, int status, const char *code, const char *message) {
    ui_response r;
    memset(&r, 0, sizeof(r));
    akeno_buf_init(&r.body, 4096);
    r.status = status;
    r.content_type = "application/json; charset=utf-8";
    akeno_buf_append_str(&r.body, "{\"ok\":false,\"error\":{\"code\":");
    akeno_buf_append_json_string(&r.body, code);
    akeno_buf_append_str(&r.body, ",\"message\":");
    akeno_buf_append_json_string(&r.body, message);
    akeno_buf_append_str(&r.body, "}}");
    send_response(fd, &r, 0);
    akeno_buf_free(&r.body);
}

static void handle_connection(akeno_app *app, int fd) {
    if (set_nonblocking(fd) != 0) return;
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    long long deadline = akeno_now_ms() + READ_DEADLINE_MS;
    akeno_buf in;
    akeno_buf_init(&in, UI_HTTP_MAX_HEAD + UI_HTTP_MAX_BODY);
    ui_request req;
    int parsed = UI_PARSE_INCOMPLETE;
    for (;;) {
        if (parsed == UI_PARSE_OK) {
            size_t want = req.head_len + (size_t)(req.content_length > 0 ? req.content_length : 0);
            if (in.len >= want) break;
        }
        int ready = akeno_wait_fd(fd, POLLIN, akeno_remaining_ms(deadline));
        if (ready <= 0) goto done; /* Timed out or failed: just close. */
        char chunk[4096];
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) goto done;
        if (akeno_buf_append(&in, chunk, (size_t)n) != 0) {
            send_error(fd, 413, "TOO_LARGE", "Request too large.");
            goto done;
        }
        if (parsed != UI_PARSE_OK) {
            parsed = ui_http_parse_request(in.data, in.len, &req);
            if (parsed == UI_PARSE_TOO_LARGE) { send_error(fd, 431, "TOO_LARGE", "Request too large."); goto done; }
            if (parsed == UI_PARSE_UNSUPPORTED) { send_error(fd, 501, "UNSUPPORTED", "Chunked requests are not supported."); goto done; }
            if (parsed == UI_PARSE_BAD) { send_error(fd, 400, "BAD_REQUEST", "Malformed request."); goto done; }
        }
    }
    /* DNS rebinding and cross-site defences: exact loopback Host, same-origin Origin. */
    if (!ui_http_host_allowed(req.host, app->port)) {
        send_error(fd, 403, "BAD_HOST", "Requests must use http://127.0.0.1.");
        goto done;
    }
    if (!ui_http_origin_allowed(req.origin, app->port)) {
        send_error(fd, 403, "BAD_ORIGIN", "Cross-site requests are not allowed.");
        goto done;
    }
    {
        int head_only = strcmp(req.method, "HEAD") == 0;
        if (head_only) memcpy(req.method, "GET", 4);
        ui_response r;
        memset(&r, 0, sizeof(r));
        akeno_buf_init(&r.body, RESPONSE_LIMIT);
        size_t body_len = (size_t)(req.content_length > 0 ? req.content_length : 0);
        app_route(app, &req, body_len ? in.data + req.head_len : NULL, body_len, &r);
        if (r.status == 0) {
            r.status = 500;
            r.content_type = NULL;
        }
        send_response(fd, &r, head_only);
        akeno_buf_free(&r.body);
    }
done:
    akeno_buf_free(&in);
}

void ui_server_loop(akeno_app *app, int listen_fd) {
    while (!app->stop && !app_exit_requested()) {
        int ready = akeno_wait_fd(listen_fd, POLLIN, 250);
        app_tick(app);
        if (ready <= 0) continue;
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) continue;
        handle_connection(app, fd);
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

int ui_server_signal_existing(int port, const char *instance_key) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int result = -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        char request[300];
        int n = snprintf(request, sizeof(request),
                         "POST /api/instance/show HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nX-Akeno-Instance-Key: %s\r\n"
                         "Content-Length: 0\r\nConnection: close\r\n\r\n",
                         port, instance_key);
        set_nonblocking(fd);
        long long deadline = akeno_now_ms() + 5000;
        if (n > 0 && write_all(fd, request, (size_t)n, deadline) == 0) {
            char reply[64] = "";
            size_t got = 0;
            while (got < 12 && akeno_wait_fd(fd, POLLIN, akeno_remaining_ms(deadline)) > 0) {
                ssize_t r = recv(fd, reply + got, sizeof(reply) - 1 - got, 0);
                if (r <= 0) break;
                got += (size_t)r;
            }
            reply[got] = '\0';
            result = strncmp(reply, "HTTP/1.1 200", 12) == 0 ? 0 : -1;
        }
    }
    close(fd);
    return result;
}
