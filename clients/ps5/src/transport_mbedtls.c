/*
 * BSD sockets + mbedTLS implementation of transport.h.
 *
 * This is the console transport: the PS5 build uses it with the payload SDK's libc, and the
 * Linux build compiles the very same file so that the complete unit and end-to-end suites run
 * against it. Security properties (identical to the OpenSSL transport):
 *   - TLS 1.2 minimum (TLS 1.3 preferred), certificate chain verification is mandatory,
 *   - the host name (or IP address) of the server URL must match the certificate,
 *   - trusted roots come from the configured CA file, or from the Mozilla bundle embedded at
 *     build time; there is no "insecure" mode and no fallback that skips verification,
 *   - randomness comes from the kernel CSPRNG only (see akeno_platform_entropy); if it fails,
 *     TLS initialization fails.
 */
#if defined(__linux__)
#define _GNU_SOURCE /* getrandom() */
#endif
#include "transport.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/version.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include "crypto_util.h"
#include "embed.h"
#include "net_posix.h"

#ifndef AKENO_CA_BUNDLE_PATH
#error "AKENO_CA_BUNDLE_PATH must name the PEM bundle to embed (see scripts/fetch-deps.sh)"
#endif
#ifndef AKENO_CA_BUNDLE_NAME
#define AKENO_CA_BUNDLE_NAME "embedded CA bundle"
#endif
AKENO_EMBED_FILE(akeno_ca_bundle, AKENO_CA_BUNDLE_PATH);

#define CA_FILE_MAX_BYTES (4u << 20)
#define ERR_IO_FAILED (-0x7F01) /* Private codes returned by the BIO callbacks. */
#define ERR_IO_RESET (-0x7F02)

/* ---- Entropy ------------------------------------------------------------------------------ */

static const char *g_entropy_name = "not initialized";

/* Fills buf from the kernel CSPRNG. Returns 0 on success, -1 if no strong source works. */
static int platform_entropy(unsigned char *buf, size_t len) {
#if defined(__linux__)
    size_t done = 0;
    while (done < len) {
        ssize_t n = getrandom(buf + done, len - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (size_t)n;
    }
    if (done == len) {
        g_entropy_name = "getrandom()";
        return 0;
    }
#elif defined(__FreeBSD__)
    /* kern.arandom: the FreeBSD kernel CSPRNG (the payload SDK's own libc seeds from it too). */
    size_t done = 0;
    while (done < len) {
        int mib[2] = { CTL_KERN, KERN_ARND };
        size_t chunk = len - done > 256 ? 256 : len - done;
        size_t got = chunk;
        if (sysctl(mib, 2, buf + done, &got, NULL, 0) != 0 || got == 0) break;
        done += got;
    }
    if (done == len) {
        g_entropy_name = "kern.arandom sysctl";
        return 0;
    }
#endif
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (got != len) return -1;
    g_entropy_name = "/dev/urandom";
    return 0;
}

/* Strong entropy source for mbedTLS and PSA (MBEDTLS_ENTROPY_HARDWARE_ALT). */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    *olen = 0;
    if (platform_entropy(output, len) != 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

/* ---- Global state ------------------------------------------------------------------------- */

typedef struct {
    mbedtls_x509_crt roots;
    mbedtls_ssl_config conf;
    int trusted;
    int skipped;
} trust_config;

static struct {
    pthread_mutex_t lock;
    int initialized;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    /* Trust store for the first CA source used (all connections of a client share it). */
    char cached_key[1100];
    trust_config *cached;
} g = { .lock = PTHREAD_MUTEX_INITIALIZER };

struct akeno_conn {
    int fd;
    int tls;
    int handshake_done;
    mbedtls_ssl_context ssl;
    trust_config *private_trust; /* Owned when the CA source differs from the cached one. */
};

static void tls_error(char *err, size_t err_len, const char *what, int code) {
    char detail[160] = "";
    if (code) mbedtls_strerror(code, detail, sizeof(detail));
    snprintf(err, err_len, "%s%s%s", what, detail[0] ? ": " : "", detail);
}

akeno_status akeno_transport_init(void) {
    pthread_mutex_lock(&g.lock);
    akeno_status status = AKENO_OK;
    if (!g.initialized) {
        if (psa_crypto_init() != PSA_SUCCESS) {
            status = AKENO_ERR_TLS;
        } else {
            mbedtls_entropy_init(&g.entropy);
            mbedtls_ctr_drbg_init(&g.drbg);
            static const char personalization[] = "akeno-device-client";
            if (mbedtls_ctr_drbg_seed(&g.drbg, mbedtls_entropy_func, &g.entropy, (const unsigned char *)personalization,
                                      sizeof(personalization) - 1) != 0) {
                mbedtls_ctr_drbg_free(&g.drbg);
                mbedtls_entropy_free(&g.entropy);
                mbedtls_psa_crypto_free();
                status = AKENO_ERR_TLS;
            } else {
                g.initialized = 1;
            }
        }
    }
    pthread_mutex_unlock(&g.lock);
    return status;
}

static void trust_free(trust_config *t) {
    if (!t) return;
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->roots);
    free(t);
}

void akeno_transport_cleanup(void) {
    pthread_mutex_lock(&g.lock);
    if (g.initialized) {
        trust_free(g.cached);
        g.cached = NULL;
        g.cached_key[0] = '\0';
        mbedtls_ctr_drbg_free(&g.drbg);
        mbedtls_entropy_free(&g.entropy);
        mbedtls_psa_crypto_free();
        g.initialized = 0;
    }
    pthread_mutex_unlock(&g.lock);
}

int akeno_random_bytes(void *out, size_t len) {
    if (!g.initialized) return -1;
    /* ctr_drbg locks internally (MBEDTLS_THREADING_C). */
    return mbedtls_ctr_drbg_random(&g.drbg, out, len) == 0 ? 0 : -1;
}

/* ---- Trust store -------------------------------------------------------------------------- */

static akeno_status read_file(const char *path, unsigned char **out, size_t *out_len, char *err, size_t err_len) {
    *out = NULL;
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || (unsigned long long)st.st_size > CA_FILE_MAX_BYTES) {
        if (fd >= 0) close(fd);
        snprintf(err, err_len, "Could not load trusted certificates from %s", path);
        return AKENO_ERR_TLS;
    }
    size_t size = (size_t)st.st_size;
    unsigned char *data = malloc(size + 1);
    size_t got = 0;
    while (data && got < size) {
        ssize_t n = read(fd, data + got, size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (!data || got != size) {
        free(data);
        snprintf(err, err_len, "Could not read trusted certificates from %s", path);
        return data ? AKENO_ERR_TLS : AKENO_ERR_NO_MEMORY;
    }
    data[size] = '\0'; /* PEM parsing requires the terminating NUL to be part of the buffer. */
    *out = data;
    *out_len = size + 1;
    return AKENO_OK;
}

static akeno_status trust_create(const char *ca_file, trust_config **out, char *err, size_t err_len) {
    *out = NULL;
    trust_config *t = calloc(1, sizeof(*t));
    if (!t) return AKENO_ERR_NO_MEMORY;
    mbedtls_x509_crt_init(&t->roots);
    mbedtls_ssl_config_init(&t->conf);

    int rc;
    if (ca_file) {
        unsigned char *pem = NULL;
        size_t pem_len = 0;
        akeno_status status = read_file(ca_file, &pem, &pem_len, err, err_len);
        if (status != AKENO_OK) {
            trust_free(t);
            return status;
        }
        rc = mbedtls_x509_crt_parse(&t->roots, pem, pem_len);
        free(pem);
    } else {
        rc = mbedtls_x509_crt_parse(&t->roots, (const unsigned char *)akeno_ca_bundle,
                                    (size_t)(akeno_ca_bundle_end - akeno_ca_bundle));
    }
    int trusted = 0;
    for (const mbedtls_x509_crt *c = &t->roots; c && c->raw.len > 0; c = c->next) trusted++;
    /* A positive result counts certificates that could not be parsed; they are ignored. */
    if (rc < 0 || trusted == 0) {
        tls_error(err, err_len, "Could not load trusted certificates", rc < 0 ? rc : 0);
        trust_free(t);
        return AKENO_ERR_TLS;
    }
    t->trusted = trusted;
    t->skipped = rc;

    rc = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        tls_error(err, err_len, "Could not set up TLS", rc);
        trust_free(t);
        return AKENO_ERR_TLS;
    }
    mbedtls_ssl_conf_min_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->conf, &t->roots, NULL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &g.drbg);
    *out = t;
    return AKENO_OK;
}

/* Returns the trust configuration for ca_file (NULL = embedded bundle), creating it once. */
static akeno_status trust_get(const char *ca_file, trust_config **shared, trust_config **owned, char *err, size_t err_len) {
    *shared = NULL;
    *owned = NULL;
    const char *key = ca_file ? ca_file : "";
    pthread_mutex_lock(&g.lock);
    akeno_status status = AKENO_OK;
    if (!g.initialized) {
        status = AKENO_ERR_TLS;
        snprintf(err, err_len, "TLS is not initialized");
    } else if (g.cached && strcmp(g.cached_key, key) == 0) {
        *shared = g.cached;
    } else if (!g.cached && strlen(key) < sizeof(g.cached_key)) {
        status = trust_create(ca_file, &g.cached, err, err_len);
        if (status == AKENO_OK) {
            snprintf(g.cached_key, sizeof(g.cached_key), "%s", key);
            *shared = g.cached;
        }
    } else {
        /* A different CA source than the cached one: build a private one for this connection. */
        status = trust_create(ca_file, owned, err, err_len);
    }
    pthread_mutex_unlock(&g.lock);
    return status;
}

/* ---- I/O callbacks ------------------------------------------------------------------------ */

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    akeno_conn *conn = ctx;
    for (;;) {
        long n = akeno_send_nosignal(conn->fd, buf, len);
        if (n >= 0) return (int)n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return errno == EPIPE || errno == ECONNRESET ? ERR_IO_RESET : ERR_IO_FAILED;
    }
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    akeno_conn *conn = ctx;
    for (;;) {
        ssize_t n = recv(conn->fd, buf, len, 0);
        if (n >= 0) return (int)n; /* 0 = end of stream */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
        return errno == ECONNRESET ? ERR_IO_RESET : ERR_IO_FAILED;
    }
}

/* ---- Connections -------------------------------------------------------------------------- */

static void conn_free(akeno_conn *conn) {
    if (conn->tls) mbedtls_ssl_free(&conn->ssl);
    trust_free(conn->private_trust);
    if (conn->fd >= 0) close(conn->fd);
    free(conn);
}

akeno_status akeno_conn_open(const akeno_url *url, const char *ca_file, int timeout_ms, akeno_conn **out, char *err,
                             size_t err_len) {
    *out = NULL;
    long long deadline = akeno_now_ms() + timeout_ms;
    akeno_conn *conn = calloc(1, sizeof(*conn));
    if (!conn) return AKENO_ERR_NO_MEMORY;
    conn->fd = -1;

    trust_config *trust = NULL;
    if (url->tls) {
        /* Load the trust store before connecting so a bad CA file fails fast and clearly. */
        trust_config *shared = NULL;
        akeno_status status = trust_get(ca_file, &shared, &conn->private_trust, err, err_len);
        if (status != AKENO_OK) {
            conn_free(conn);
            return status;
        }
        trust = shared ? shared : conn->private_trust;
    }

    akeno_status status = akeno_tcp_connect(url, deadline, &conn->fd, err, err_len);
    if (status != AKENO_OK) {
        conn_free(conn);
        return status;
    }
    if (!url->tls) {
        *out = conn;
        return AKENO_OK;
    }

    mbedtls_ssl_init(&conn->ssl);
    conn->tls = 1;
    int rc = mbedtls_ssl_setup(&conn->ssl, &trust->conf);
    /* Sets SNI and the name the certificate must match (DNS name or IP address SAN). */
    if (rc == 0) rc = mbedtls_ssl_set_hostname(&conn->ssl, url->host);
    if (rc != 0) {
        tls_error(err, err_len, "Could not set up TLS", rc);
        conn_free(conn);
        return AKENO_ERR_TLS;
    }
    mbedtls_ssl_set_bio(&conn->ssl, conn, bio_send, bio_recv, NULL);

    for (;;) {
        rc = mbedtls_ssl_handshake(&conn->ssl);
        if (rc == 0) break;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            int ready = akeno_wait_fd(conn->fd, rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, akeno_remaining_ms(deadline));
            if (ready > 0) continue;
            snprintf(err, err_len, ready == 0 ? "TLS handshake timed out" : "TLS handshake failed");
            conn_free(conn);
            return ready == 0 ? AKENO_ERR_TIMEOUT : AKENO_ERR_TLS;
        }
        uint32_t flags = mbedtls_ssl_get_verify_result(&conn->ssl);
        if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != 0 && flags != (uint32_t)-1) {
            char info[200] = "";
            mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
            size_t n = strlen(info);
            while (n > 0 && (info[n - 1] == '\n' || info[n - 1] == ' ')) info[--n] = '\0';
            for (char *p = info; *p; p++)
                if (*p == '\n') *p = ';';
            snprintf(err, err_len, "The server certificate is not trusted: %s", info);
        } else {
            tls_error(err, err_len, "TLS handshake failed", rc);
        }
        conn_free(conn);
        return AKENO_ERR_TLS;
    }
    conn->handshake_done = 1;
    *out = conn;
    return AKENO_OK;
}

int akeno_conn_wait_readable(akeno_conn *conn, int timeout_ms) {
    if (conn->tls && mbedtls_ssl_get_bytes_avail(&conn->ssl) > 0) return 1;
    int ready = akeno_wait_fd(conn->fd, POLLIN, timeout_ms);
    return ready < 0 ? -(int)AKENO_ERR_NETWORK : ready;
}

long akeno_conn_read(akeno_conn *conn, void *buf, size_t len, int timeout_ms) {
    long long deadline = akeno_now_ms() + timeout_ms;
    for (;;) {
        if (!conn->tls) {
            ssize_t n = recv(conn->fd, buf, len, 0);
            if (n >= 0) return (long)n;
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return -(long)AKENO_ERR_NETWORK;
            int ready = akeno_wait_fd(conn->fd, POLLIN, akeno_remaining_ms(deadline));
            if (ready == 0) return -(long)AKENO_ERR_TIMEOUT;
            if (ready < 0) return -(long)AKENO_ERR_NETWORK;
            continue;
        }
        int n = mbedtls_ssl_read(&conn->ssl, buf, len > (1u << 30) ? (1u << 30) : len);
        if (n > 0) return n;
        /* Only close_notify is an orderly end. A stream that stops without it may be truncated
         * and is reported as a (retryable) network error, as OpenSSL 3 does. */
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (n == 0 || n == MBEDTLS_ERR_SSL_CONN_EOF) return -(long)AKENO_ERR_NETWORK;
        if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue; /* TLS 1.3 post-handshake message. */
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            int ready = akeno_wait_fd(conn->fd, n == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, akeno_remaining_ms(deadline));
            if (ready == 0) return -(long)AKENO_ERR_TIMEOUT;
            if (ready < 0) return -(long)AKENO_ERR_NETWORK;
            continue;
        }
        if (n == ERR_IO_FAILED || n == ERR_IO_RESET) return -(long)AKENO_ERR_NETWORK;
        return -(long)AKENO_ERR_TLS;
    }
}

akeno_status akeno_conn_write_all(akeno_conn *conn, const void *buf, size_t len, int timeout_ms) {
    long long deadline = akeno_now_ms() + timeout_ms;
    const unsigned char *p = buf;
    while (len > 0) {
        if (!conn->tls) {
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
        int n = mbedtls_ssl_write(&conn->ssl, p, len > (1u << 30) ? (1u << 30) : len);
        if (n > 0) { p += n; len -= (size_t)n; continue; }
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            int ready = akeno_wait_fd(conn->fd, n == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, akeno_remaining_ms(deadline));
            if (ready == 0) return AKENO_ERR_TIMEOUT;
            if (ready > 0) continue;
        }
        return AKENO_ERR_NETWORK;
    }
    return AKENO_OK;
}

void akeno_conn_close(akeno_conn *conn) {
    if (!conn) return;
    if (conn->tls && conn->handshake_done) mbedtls_ssl_close_notify(&conn->ssl); /* Best effort, non-blocking. */
    conn_free(conn);
}

void akeno_conn_describe(const akeno_conn *conn, char *out, size_t out_len) {
    if (!conn || !conn->tls) {
        snprintf(out, out_len, "plain TCP");
        return;
    }
    snprintf(out, out_len, "%s %s", mbedtls_ssl_get_version(&conn->ssl), mbedtls_ssl_get_ciphersuite(&conn->ssl));
}

void akeno_transport_get_info(akeno_transport_info *out) {
    out->backend = "mbedTLS " MBEDTLS_VERSION_STRING;
    out->trust_store = AKENO_CA_BUNDLE_NAME;
    out->entropy = g_entropy_name;
    pthread_mutex_lock(&g.lock);
    out->trusted_roots = g.cached ? g.cached->trusted : -1;
    out->skipped_roots = g.cached ? g.cached->skipped : -1;
    pthread_mutex_unlock(&g.lock);
}
