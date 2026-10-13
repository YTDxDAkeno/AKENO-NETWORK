#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "net_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "transport.h"

long long akeno_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void akeno_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

int akeno_remaining_ms(long long deadline) {
    long long left = deadline - akeno_now_ms();
    return left > 0 ? (int)(left > 2147483647LL ? 2147483647LL : left) : 0;
}

int akeno_wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd pfd = { fd, events, 0 };
    for (;;) {
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) return -1;
        if (rc == 0) return 0;
        return 1;
    }
}

long akeno_send_nosignal(int fd, const void *buf, size_t len) {
#ifdef MSG_NOSIGNAL
    return (long)send(fd, buf, len, MSG_NOSIGNAL);
#else
    return (long)send(fd, buf, len, 0);
#endif
}

static int is_ip_literal(const char *host) {
    unsigned char addr[16];
    return inet_pton(AF_INET, host, addr) == 1 || inet_pton(AF_INET6, host, addr) == 1;
}

akeno_status akeno_tcp_connect(const akeno_url *url, long long deadline, int *out_fd, char *err, size_t err_len) {
    char port[8];
    snprintf(port, sizeof(port), "%d", url->port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    /* IP literals never go through a resolver (the PS5 resolver only handles names). */
    if (is_ip_literal(url->host)) hints.ai_flags = AI_NUMERICHOST;
    struct addrinfo *list = NULL;
    int gai = getaddrinfo(url->host, port, &hints, &list);
    if (gai != 0) {
        snprintf(err, err_len, "Could not resolve %s: %s", url->host, gai_strerror(gai));
        return AKENO_ERR_NETWORK;
    }
    akeno_status status = AKENO_ERR_NETWORK;
    snprintf(err, err_len, "Could not connect to %s:%d", url->host, url->port);
    for (struct addrinfo *ai = list; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); continue; }
#ifdef SO_NOSIGPIPE
        int one_nosig = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one_nosig, sizeof(one_nosig));
#endif
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            int ready = akeno_wait_fd(fd, POLLOUT, akeno_remaining_ms(deadline));
            if (ready == 0) {
                close(fd);
                status = AKENO_ERR_TIMEOUT;
                snprintf(err, err_len, "Timed out connecting to %s:%d", url->host, url->port);
                continue;
            }
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (ready < 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
                close(fd);
                snprintf(err, err_len, "Could not connect to %s:%d: %s", url->host, url->port, strerror(so_error ? so_error : errno));
                continue;
            }
            rc = 0;
        }
        if (rc == 0) {
            freeaddrinfo(list);
            *out_fd = fd;
            return AKENO_OK;
        }
        snprintf(err, err_len, "Could not connect to %s:%d: %s", url->host, url->port, strerror(errno));
        close(fd);
    }
    freeaddrinfo(list);
    return status;
}
