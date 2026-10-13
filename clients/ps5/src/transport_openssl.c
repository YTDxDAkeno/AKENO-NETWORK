/* POSIX sockets + OpenSSL 3 implementation of transport.h (Linux host build). */
#define _POSIX_C_SOURCE 200809L
#include "transport.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "crypto_util.h"
#include "net_posix.h"

struct akeno_conn {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
};

akeno_status akeno_transport_init(void) {
    if (OPENSSL_init_ssl(0, NULL) != 1) return AKENO_ERR_TLS;
    return AKENO_OK;
}

void akeno_transport_cleanup(void) {}

int akeno_random_bytes(void *out, size_t len) {
    return RAND_bytes(out, (int)len) == 1 ? 0 : -1;
}

static void tls_error(char *err, size_t err_len, const char *what) {
    unsigned long code = ERR_get_error();
    char detail[160] = "";
    if (code) ERR_error_string_n(code, detail, sizeof(detail));
    snprintf(err, err_len, "%s%s%s", what, detail[0] ? ": " : "", detail);
    ERR_clear_error();
}

akeno_status akeno_conn_open(const akeno_url *url, const char *ca_file, int timeout_ms, akeno_conn **out, char *err,
                             size_t err_len) {
    *out = NULL;
    long long deadline = akeno_now_ms() + timeout_ms;
    akeno_conn *conn = calloc(1, sizeof(*conn));
    if (!conn) return AKENO_ERR_NO_MEMORY;
    conn->fd = -1;
    akeno_status status = akeno_tcp_connect(url, deadline, &conn->fd, err, err_len);
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
            int ready = akeno_wait_fd(conn->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, akeno_remaining_ms(deadline));
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
    int ready = akeno_wait_fd(conn->fd, POLLIN, timeout_ms);
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
            int ready = akeno_wait_fd(conn->fd, POLLIN, akeno_remaining_ms(deadline));
            if (ready == 0) return -(long)AKENO_ERR_TIMEOUT;
            if (ready < 0) return -(long)AKENO_ERR_NETWORK;
            continue;
        }
        int n = SSL_read(conn->ssl, buf, len > 1 << 30 ? 1 << 30 : (int)len);
        if (n > 0) return n;
        int e = SSL_get_error(conn->ssl, n);
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int ready = akeno_wait_fd(conn->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, akeno_remaining_ms(deadline));
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
            long n = akeno_send_nosignal(conn->fd, p, len);
            if (n > 0) { p += n; len -= (size_t)n; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                int ready = akeno_wait_fd(conn->fd, POLLOUT, akeno_remaining_ms(deadline));
                if (ready == 0) return AKENO_ERR_TIMEOUT;
                if (ready > 0) continue;
            }
            return AKENO_ERR_NETWORK;
        }
        int n = SSL_write(conn->ssl, p, len > 1 << 30 ? 1 << 30 : (int)len);
        if (n > 0) { p += n; len -= (size_t)n; continue; }
        int e = SSL_get_error(conn->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int ready = akeno_wait_fd(conn->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, akeno_remaining_ms(deadline));
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

void akeno_conn_describe(const akeno_conn *conn, char *out, size_t out_len) {
    if (!conn || !conn->ssl) {
        snprintf(out, out_len, "plain TCP");
        return;
    }
    snprintf(out, out_len, "%s %s", SSL_get_version(conn->ssl), SSL_get_cipher_name(conn->ssl));
}

void akeno_transport_get_info(akeno_transport_info *out) {
    out->backend = OpenSSL_version(OPENSSL_VERSION);
    out->trust_store = "system trust store";
    out->entropy = "OpenSSL RAND_bytes";
    out->trusted_roots = -1;
    out->skipped_roots = -1;
}
