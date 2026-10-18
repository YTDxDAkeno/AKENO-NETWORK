#define _POSIX_C_SOURCE 200809L
#include "update_net.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "http.h"
#include "transport.h"
#include "update_fs.h"
#include "url.h"

#define WHY(...)                                                 \
    do {                                                         \
        if (why && why_len) snprintf(why, why_len, __VA_ARGS__); \
    } while (0)

#define MAX_REDIRECTS 5

const char *const *akn_update_github_hosts(void) {
    static const char *const hosts[] = { "api.github.com", "github.com", "objects.githubusercontent.com",
                                         "release-assets.githubusercontent.com", "github-releases.githubusercontent.com",
                                         NULL };
    return hosts;
}

int akn_update_split_url(const char *url, int *tls, char *host, size_t host_len, int *port, char *target, size_t target_len) {
    const char *p;
    if (strncmp(url, "https://", 8) == 0) {
        *tls = 1;
        *port = 443;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        *tls = 0;
        *port = 80;
        p = url + 7;
    } else {
        return -1;
    }
    size_t hl = strcspn(p, ":/?#");
    if (hl == 0 || hl >= host_len) return -1;
    for (size_t i = 0; i < hl; i++)
        if (!(isalnum((unsigned char)p[i]) || p[i] == '.' || p[i] == '-')) return -1; /* no user info, no IPv6 */
    memcpy(host, p, hl);
    host[hl] = '\0';
    p += hl;
    if (*p == ':') {
        char *end = NULL;
        long v = strtol(p + 1, &end, 10);
        if (end == p + 1 || v < 1 || v > 65535) return -1;
        *port = (int)v;
        p = end;
    }
    if (*p == '\0') p = "/";
    if (*p != '/' || strlen(p) >= target_len) return -1;
    for (const char *c = p; *c; c++)
        if ((unsigned char)*c <= 0x20 || (unsigned char)*c >= 0x7F || *c == '#') return -1;
    snprintf(target, target_len, "%s", p);
    return 0;
}

static int host_allowed(const char *const *hosts, const char *host) {
    for (int i = 0; hosts && hosts[i]; i++)
        if (strcasecmp(hosts[i], host) == 0) return 1;
    return 0;
}

/* A header's value from the raw head ("Name: value\r\n"), case-insensitive name. */
static int header_value(const char *head, size_t len, const char *name, char *out, size_t out_len) {
    size_t nl = strlen(name);
    const char *p = memchr(head, '\n', len);
    const char *end = head + len;
    while (p && p + 1 < end) {
        const char *line = p + 1;
        const char *eol = memchr(line, '\n', (size_t)(end - line));
        if (!eol) break;
        if ((size_t)(eol - line) > nl + 1 && strncasecmp(line, name, nl) == 0 && line[nl] == ':') {
            const char *v = line + nl + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            const char *ve = eol;
            while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
            size_t n = (size_t)(ve - v);
            if (n >= out_len) return -1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
        p = eol;
    }
    return -1;
}

typedef struct {
    akeno_buf *mem;
    int fd;
    long long done, max;
    int failed_storage;
} sink;

static int sink_write(sink *s, const char *data, size_t len) {
    if (s->done + (long long)len > s->max) return -1;
    if (s->fd >= 0) {
        size_t off = 0;
        while (off < len) {
            ssize_t n = write(s->fd, data + off, len - off);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                s->failed_storage = errno ? errno : EIO;
                return -1;
            }
            off += (size_t)n;
        }
    } else if (akeno_buf_append(s->mem, data, len) != 0) {
        return -1;
    }
    s->done += (long long)len;
    return 0;
}

/* Streaming "Transfer-Encoding: chunked" decoder. */
typedef struct {
    int state;         /* 0 size line, 1 data, 2 CRLF after data, 3 trailers, 4 done */
    char line[32];
    size_t line_len;
    unsigned long long left;
} chunker;

static int chunk_feed(chunker *c, const char *in, size_t len, sink *s) {
    size_t i = 0;
    while (i < len && c->state != 4) {
        if (c->state == 1) {
            size_t n = len - i;
            if ((unsigned long long)n > c->left) n = (size_t)c->left;
            if (sink_write(s, in + i, n) != 0) return -1;
            c->left -= n;
            i += n;
            if (c->left == 0) c->state = 2;
            continue;
        }
        char ch = in[i++];
        if (ch != '\n') {
            if (ch != '\r') {
                if (c->line_len + 1 >= sizeof(c->line)) return -1;
                c->line[c->line_len++] = ch;
            }
            continue;
        }
        c->line[c->line_len] = '\0';
        if (c->state == 0) {
            char *end = NULL;
            errno = 0;
            unsigned long long n = strtoull(c->line, &end, 16);
            if (end == c->line || errno || (*end && *end != ';')) return -1;
            c->left = n;
            c->state = n == 0 ? 3 : 1;
        } else if (c->state == 2) {
            if (c->line_len != 0) return -1;
            c->state = 0;
        } else if (c->state == 3) {
            if (c->line_len == 0) c->state = 4;
        }
        c->line_len = 0;
    }
    return 0;
}

/* akeno_conn_read returns a negated akeno_status on errors. */
static akeno_status read_error(long n) {
    if (n == -(long)AKENO_ERR_TIMEOUT) return AKENO_ERR_TIMEOUT;
    if (n == -(long)AKENO_ERR_TLS) return AKENO_ERR_TLS;
    return AKENO_ERR_NETWORK;
}

static akn_dl_result net_error(akeno_status st, const char *host, char *why, size_t why_len) {
    if (st == AKENO_ERR_TLS)
        WHY("The secure connection to %s could not be verified (TLS). Check the console's date and time.", host);
    else if (st == AKENO_ERR_TIMEOUT)
        WHY("%s did not answer in time. Check the console's internet connection.", host);
    else
        WHY("%s could not be reached. Check the console's internet connection and DNS (it must resolve %s).", host, host);
    return AKN_DL_NETWORK;
}

akn_dl_result akn_update_get(const akn_dl_config *cfg, const char *url, const char *accept, long long max_bytes,
                             akeno_buf *mem, const char *file, const akn_dl_hooks *hooks, int *http_status, char *why,
                             size_t why_len) {
    if (why && why_len) why[0] = '\0';
    if (http_status) *http_status = 0;
    int connect_ms = cfg->connect_timeout_ms > 0 ? cfg->connect_timeout_ms : 15000;
    int idle_ms = cfg->idle_timeout_ms > 0 ? cfg->idle_timeout_ms : 20000;
    char current[1024];
    snprintf(current, sizeof(current), "%s", url);
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        int tls = 0, port = 0;
        char host[256], target[900];
        if (akn_update_split_url(current, &tls, host, sizeof(host), &port, target, sizeof(target)) != 0) {
            WHY("Refused a malformed download address.");
            return AKN_DL_REFUSED;
        }
        if (!host_allowed(cfg->hosts, host)) {
            WHY("Refused to download from %s (not an official GitHub address).", host);
            return AKN_DL_REFUSED;
        }
        akeno_url u;
        memset(&u, 0, sizeof(u));
        u.tls = tls;
        u.port = port;
        snprintf(u.host, sizeof(u.host), "%s", host);
        if (!tls && !akeno_url_is_loopback(&u)) {
            WHY("Refused an unencrypted download from %s.", host);
            return AKN_DL_REFUSED;
        }
        if (hooks && hooks->cancel && *hooks->cancel) {
            WHY("Cancelled.");
            return AKN_DL_CANCELLED;
        }
        akeno_conn *conn = NULL;
        char err[160] = "";
        akeno_status st = akeno_conn_open(&u, cfg->ca_file, connect_ms, &conn, err, sizeof(err));
        if (st != AKENO_OK) return net_error(st, host, why, why_len);
        akeno_buf req;
        /* RFC 9110 7.2: the port belongs in Host when it is not the scheme's default. */
        char host_header[300];
        if (port == (tls ? 443 : 80)) snprintf(host_header, sizeof(host_header), "%s", host);
        else snprintf(host_header, sizeof(host_header), "%s:%d", host, port);
        akeno_buf_init(&req, 4096);
        akeno_buf_printf(&req,
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: %s\r\nAccept-Encoding: identity\r\n"
                         "X-GitHub-Api-Version: 2022-11-28\r\nConnection: close\r\n\r\n",
                         target, host_header, cfg->user_agent ? cfg->user_agent : "AKENO-Native", accept ? accept : "*/*");
        st = req.data ? akeno_conn_write_all(conn, req.data, req.len, idle_ms) : AKENO_ERR_NO_MEMORY;
        akeno_buf_free(&req);
        if (st != AKENO_OK) {
            akeno_conn_close(conn);
            return net_error(st, host, why, why_len);
        }
        /* Head */
        char headbuf[AKENO_HTTP_MAX_HEAD + 1];
        size_t hlen = 0;
        akeno_http_head head;
        int parsed = 0;
        while (!parsed) {
            if (hooks && hooks->cancel && *hooks->cancel) {
                akeno_conn_close(conn);
                WHY("Cancelled.");
                return AKN_DL_CANCELLED;
            }
            if (hlen >= AKENO_HTTP_MAX_HEAD) {
                akeno_conn_close(conn);
                WHY("%s sent an oversized answer.", host);
                return AKN_DL_REFUSED;
            }
            long n = akeno_conn_read(conn, headbuf + hlen, AKENO_HTTP_MAX_HEAD - hlen, idle_ms);
            if (n <= 0) {
                akeno_conn_close(conn);
                return net_error(n == 0 ? AKENO_ERR_NETWORK : read_error(n), host, why, why_len);
            }
            hlen += (size_t)n;
            int r = akeno_http_parse_head(headbuf, hlen, &head);
            if (r < 0) {
                akeno_conn_close(conn);
                WHY("%s sent a malformed answer.", host);
                return AKN_DL_REFUSED;
            }
            parsed = r == 1;
        }
        headbuf[hlen] = '\0';
        if (http_status) *http_status = head.status;
        if (head.status == 301 || head.status == 302 || head.status == 303 || head.status == 307 || head.status == 308) {
            char location[1024];
            int got = header_value(headbuf, head.head_len, "Location", location, sizeof(location));
            akeno_conn_close(conn);
            if (got != 0 || strncmp(location, "http", 4) != 0) {
                WHY("%s redirected to an address AKENO does not follow.", host);
                return AKN_DL_REFUSED;
            }
            snprintf(current, sizeof(current), "%s", location);
            continue;
        }
        if (head.status != 200) {
            char remaining[16] = "";
            header_value(headbuf, head.head_len, "X-RateLimit-Remaining", remaining, sizeof(remaining));
            akeno_conn_close(conn);
            if ((head.status == 403 || head.status == 429) && strcmp(remaining, "0") == 0)
                WHY("GitHub limits how often a console may check for updates (60 times per hour). Try again later.");
            else if (head.status == 404)
                WHY("GitHub answered \"not found\" (HTTP 404) for %s.", host);
            else
                WHY("GitHub answered HTTP %d.", head.status);
            return AKN_DL_HTTP;
        }
        if (head.content_length > max_bytes) {
            akeno_conn_close(conn);
            WHY("The download is larger than allowed (%lld bytes).", head.content_length);
            return AKN_DL_REFUSED;
        }
        sink s = { mem, -1, 0, max_bytes, 0 };
        if (file) {
            s.fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (s.fd < 0) {
                int e = errno;
                akeno_conn_close(conn);
                const char *t = akn_fs_error_text(e);
                WHY("The download cannot be saved: %s", t ? t : strerror(e));
                return AKN_DL_STORAGE;
            }
        }
        long long total = head.chunked ? -1 : head.content_length;
        chunker ck;
        memset(&ck, 0, sizeof(ck));
        akn_dl_result res = AKN_DL_OK;
        const char *extra = headbuf + head.head_len;
        size_t extra_len = hlen - head.head_len;
        char buf[16384];
        int finished = 0;
        while (!finished && res == AKN_DL_OK) {
            const char *data;
            size_t n;
            if (extra_len) {
                data = extra;
                n = extra_len;
                extra_len = 0;
            } else {
                if (hooks && hooks->cancel && *hooks->cancel) {
                    WHY("Cancelled.");
                    res = AKN_DL_CANCELLED;
                    break;
                }
                long r = akeno_conn_read(conn, buf, sizeof(buf), idle_ms);
                if (r < 0) {
                    res = net_error(read_error(r), host, why, why_len);
                    if (s.done > 0) WHY("The connection to %s was lost after %lld bytes. Retry to download again.", host, s.done);
                    break;
                }
                if (r == 0) { /* closed */
                    if (head.chunked ? ck.state != 4 : (total >= 0 && s.done < total)) {
                        WHY("The connection to %s ended early (%lld of %lld bytes). Retry to download again.", host, s.done, total);
                        res = AKN_DL_NETWORK;
                    }
                    break;
                }
                data = buf;
                n = (size_t)r;
            }
            int bad;
            if (head.chunked) {
                bad = chunk_feed(&ck, data, n, &s);
                if (ck.state == 4) finished = 1;
            } else {
                if (total >= 0 && s.done + (long long)n > total) n = (size_t)(total - s.done);
                bad = sink_write(&s, data, n);
                if (total >= 0 && s.done == total) finished = 1;
            }
            if (bad) {
                if (s.failed_storage) {
                    const char *t = akn_fs_error_text(s.failed_storage);
                    WHY("The download cannot be saved: %s", t ? t : strerror(s.failed_storage));
                    res = AKN_DL_STORAGE;
                } else {
                    WHY("The download is malformed or larger than allowed.");
                    res = AKN_DL_REFUSED;
                }
            }
            if (hooks && hooks->progress) hooks->progress(hooks->ctx, s.done, total);
        }
        akeno_conn_close(conn);
        if (s.fd >= 0) {
            if (res == AKN_DL_OK && fsync(s.fd) != 0) {
                const char *t = akn_fs_error_text(errno);
                WHY("The download cannot be saved: %s", t ? t : strerror(errno));
                res = AKN_DL_STORAGE;
            }
            close(s.fd);
            if (res != AKN_DL_OK && file) unlink(file);
        }
        return res;
    }
    WHY("Too many redirects.");
    return AKN_DL_REFUSED;
}
