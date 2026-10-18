#define _POSIX_C_SOURCE 200809L
#include "lab_net.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "akeno/json.h"

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int connect_to(const char *host, int port, int timeout_ms) {
    char service[8];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &res) != 0 || !res) return -1;
    int fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    if (rc != 0) {
        struct pollfd p = { fd, POLLOUT, 0 };
        int err = 0;
        socklen_t len = sizeof(err);
        if (poll(&p, 1, timeout_ms) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL, flags);
    return fd;
}

int akn_lab_probe_tcp(const char *host, int port, int timeout_ms, int *ms) {
    long long t = now_ms();
    int fd = connect_to(host, port, timeout_ms);
    *ms = (int)(now_ms() - t);
    if (fd < 0) return 1;
    close(fd);
    return 0;
}

int akn_lab_probe_http(const char *host, int port, const char *path, int timeout_ms, int *ms) {
    long long t = now_ms();
    int fd = connect_to(host, port, timeout_ms);
    if (fd < 0) {
        *ms = (int)(now_ms() - t);
        return 1;
    }
    char req[256];
    int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    int ok = 0;
    if (n > 0 && send(fd, req, (size_t)n, 0) == n) {
        struct pollfd p = { fd, POLLIN, 0 };
        char buf[16];
        if (poll(&p, 1, timeout_ms) == 1 && recv(fd, buf, sizeof(buf), 0) >= 8 && memcmp(buf, "HTTP/1.", 7) == 0) ok = 1;
    }
    close(fd);
    *ms = (int)(now_ms() - t);
    return ok ? 0 : 1;
}

int akn_lab_server_from_settings(const char *path, char *host, size_t host_len, int *port) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    char data[4096];
    ssize_t n = -1;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < (off_t)sizeof(data))
        n = read(fd, data, sizeof(data));
    close(fd);
    if (n <= 0) return -1;
    akeno_json_doc *doc = akeno_json_parse(data, (size_t)n, NULL, 0);
    const char *server = akeno_json_get_string(akeno_json_root(doc), "server", "");
    int rc = -1;
    const char *p = server;
    int def = 443;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        def = 80;
    } else {
        p = NULL;
    }
    if (p) {
        size_t hl = strcspn(p, ":/");
        if (hl > 0 && hl < host_len) {
            memcpy(host, p, hl);
            host[hl] = '\0';
            *port = def;
            if (p[hl] == ':') {
                int v = 0;
                if (sscanf(p + hl + 1, "%d", &v) == 1 && v > 0 && v < 65536) *port = v;
            }
            rc = 0;
        }
    }
    akeno_json_free(doc);
    return rc;
}
