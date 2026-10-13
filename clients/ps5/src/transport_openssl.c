/* POSIX sockets + OpenSSL 3 implementation of transport.h. */
#define _POSIX_C_SOURCE 200809L
#include "transport.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "crypto_util.h"

struct akeno_conn {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
};

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

akeno_status akeno_transport_init(void) {
    if (OPENSSL_init_ssl(0, NULL) != 1) return AKENO_ERR_TLS;
    return AKENO_OK;
}

void akeno_transport_cleanup(void) {}

int akeno_random_bytes(void *out, size_t len) {
    return RAND_bytes(out, (int)len) == 1 ? 0 : -1;
}

static int remaining(long long deadline) {
    long long left = deadline - akeno_now_ms();
    return left > 0 ? (int)(left > 2147483647LL ? 2147483647LL : left) : 0;
}

/* Waits for the socket; returns 1 ready, 0 timeout, -1 error. */
static int wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd pfd = { fd, events, 0 };
    for (;;) {
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) return -1;
        if (rc == 0) return 0;
        return 1;
    }
}

static void tls_error(char *err, size_t err_len, const char *what) {
    unsigned long code = ERR_get_error();
    char detail[160] = "";
    if (code) ERR_error_string_n(code, detail, sizeof(detail));
    snprintf(err, err_len, "%s%s%s", what, detail[0] ? ": " : "", detail);
    ERR_clear_error();
}

static akeno_status tcp_connect(const akeno_url *url, long long deadline, int *out_fd, char *err, size_t err_len) {
    char port[8];
    snprintf(port, sizeof(port), "%d", url->port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
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
            int ready = wait_fd(fd, POLLOUT, remaining(deadline));
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

akeno_status akeno_conn_open(const akeno_url *url, const char *ca_file, int timeout_ms, akeno_conn **out, char *err,
                             size_t err_len) {
    *out = NULL;
    long long deadline = akeno_now_ms() + timeout_ms;
    akeno_conn *conn = calloc(1, sizeof(*conn));
    if (!conn) return AKENO_ERR_NO_MEMORY;
    conn->fd = -1;
    akeno_status status = tcp_connect(url, deadline, &conn->fd, err, err_len);
    if (status != AKENO_OK) {
        free(conn);
        return status;
    }
    if (!url->tls) {
        *out = conn;
        return AKENO_OK;
    }

    conn->ctx = SSL_CTX_new(TLS_client_method());
    if (!conn->ctx) goto tls_fail;
    SSL_CTX_set_min_proto_version(conn->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(conn->ctx, SSL_VERIFY_PEER, NULL);
    if (ca_file ? SSL_CTX_load_verify_locations(conn->ctx, ca_file, NULL) != 1
                : SSL_CTX_set_default_verify_paths(conn->ctx) != 1) {
        tls_error(err, err_len, "Could not load trusted certificates");
        akeno_conn_close(conn);
        return AKENO_ERR_TLS;
    }
    conn->ssl = SSL_new(conn->ctx);
    if (!conn->ssl || SSL_set_fd(conn->ssl, conn->fd) != 1) goto tls_fail;
    /* SNI and host name verification (IP literals are matched against IP SANs). */
    SSL_set_hostflags(conn->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (SSL_set1_host(conn->ssl, url->host) != 1) goto tls_fail;
    if (!strchr(url->host, ':')) SSL_set_tlsext_host_name(conn->ssl, url->host);

    for (;;) {
        int rc = SSL_connect(conn->ssl);
        if (rc == 1) break;
        int e = SSL_get_error(conn->ssl, rc);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int ready = wait_fd(conn->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, remaining(deadline));
            if (ready > 0) continue;
            snprintf(err, err_len, ready == 0 ? "TLS handshake timed out" : "TLS handshake failed");
            akeno_conn_close(conn);
            return ready == 0 ? AKENO_ERR_TIMEOUT : AKENO_ERR_TLS;
        }
        long verify = SSL_get_verify_result(conn->ssl);
        if (verify != X509_V_OK) {
            snprintf(err, err_len, "The server certificate is not trusted: %s", X509_verify_cert_error_string(verify));
            ERR_clear_error();
        } else {
            tls_error(err, err_len, "TLS handshake failed");
        }
        akeno_conn_close(conn);
        return AKENO_ERR_TLS;
    }
    *out = conn;
    return AKENO_OK;

tls_fail:
    tls_error(err, err_len, "Could not set up TLS");
    akeno_conn_close(conn);
    return AKENO_ERR_TLS;
}

int akeno_conn_wait_readable(akeno_conn *conn, int timeout_ms) {
    if (conn->ssl && SSL_pending(conn->ssl) > 0) return 1;
    int ready = wait_fd(conn->fd, POLLIN, timeout_ms);
    return ready < 0 ? -(int)AKENO_ERR_NETWORK : ready;
}

long akeno_conn_read(akeno_conn *conn, void *buf, size_t len, int timeout_ms) {
    long long deadline = akeno_now_ms() + timeout_ms;
    for (;;) {
        if (!conn->ssl) {
            ssize_t n = recv(conn->fd, buf, len, 0);
            if (n >= 0) return (long)n;
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return -(long)AKENO_ERR_NETWORK;
            int ready = wait_fd(conn->fd, POLLIN, remaining(deadline));
            if (ready == 0) return -(long)AKENO_ERR_TIMEOUT;
            if (ready < 0) return -(long)AKENO_ERR_NETWORK;
            continue;
        }
        int n = SSL_read(conn->ssl, buf, len > 1 << 30 ? 1 << 30 : (int)len);
        if (n > 0) return n;
        int e = SSL_get_error(conn->ssl, n);
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int ready = wait_fd(conn->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, remaining(deadline));
            if (ready == 0) return -(long)AKENO_ERR_TIMEOUT;
            if (ready < 0) return -(long)AKENO_ERR_NETWORK;
            continue;
        }
        ERR_clear_error();
        /* A peer that closes without close_notify (common behind proxies) ends the stream. */
        if (e == SSL_ERROR_SYSCALL || e == SSL_ERROR_SSL) return e == SSL_ERROR_SYSCALL && errno == 0 ? 0 : -(long)AKENO_ERR_NETWORK;
        return -(long)AKENO_ERR_TLS;
    }
}

akeno_status akeno_conn_write_all(akeno_conn *conn, const void *buf, size_t len, int timeout_ms) {
    long long deadline = akeno_now_ms() + timeout_ms;
    const char *p = buf;
    while (len > 0) {
        if (!conn->ssl) {
#ifdef MSG_NOSIGNAL
            ssize_t n = send(conn->fd, p, len, MSG_NOSIGNAL);
#else
            ssize_t n = send(conn->fd, p, len, 0);
#endif
            if (n > 0) { p += n; len -= (size_t)n; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                int ready = wait_fd(conn->fd, POLLOUT, remaining(deadline));
                if (ready == 0) return AKENO_ERR_TIMEOUT;
                if (ready > 0) continue;
            }
            return AKENO_ERR_NETWORK;
        }
        int n = SSL_write(conn->ssl, p, len > 1 << 30 ? 1 << 30 : (int)len);
        if (n > 0) { p += n; len -= (size_t)n; continue; }
        int e = SSL_get_error(conn->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int ready = wait_fd(conn->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, remaining(deadline));
            if (ready == 0) return AKENO_ERR_TIMEOUT;
            if (ready > 0) continue;
        }
        ERR_clear_error();
        return AKENO_ERR_NETWORK;
    }
    return AKENO_OK;
}

void akeno_conn_close(akeno_conn *conn) {
    if (!conn) return;
    if (conn->ssl) {
        SSL_shutdown(conn->ssl); /* Best effort; non-blocking, never waits. */
        SSL_free(conn->ssl);
    }
    if (conn->ctx) SSL_CTX_free(conn->ctx);
    if (conn->fd >= 0) close(conn->fd);
    ERR_clear_error();
    free(conn);
}
