/*
 * HTTPS downloads of the updater over the client library's transport (certificate chain and host
 * name verified against the built-in Mozilla CA bundle). Plain HTTP is refused except for
 * loopback hosts (the mock GitHub of the tests). Redirects (GitHub sends release downloads to its
 * asset host) are followed at most five times and only to the configured hosts.
 */
#ifndef AKN_UPDATE_NET_H
#define AKN_UPDATE_NET_H

#include <stddef.h>

#include "buf.h"

typedef struct {
    const char *ca_file;               /* NULL: the built-in bundle */
    const char *const *hosts;          /* NULL-terminated allow-list of host names */
    const char *user_agent;
    int connect_timeout_ms;            /* 0: 15000 */
    int idle_timeout_ms;               /* 0: 20000 (no byte for this long fails the download) */
} akn_dl_config;

/* The official hosts: api.github.com, github.com and GitHub's release-asset hosts. */
const char *const *akn_update_github_hosts(void);

typedef struct {
    volatile int *cancel;              /* polled between reads */
    void (*progress)(void *ctx, long long done, long long total); /* total -1 when unknown */
    void *ctx;
} akn_dl_hooks;

typedef enum {
    AKN_DL_OK = 0,
    AKN_DL_CANCELLED,
    AKN_DL_NETWORK,       /* DNS, connect, TLS, timeout, connection lost */
    AKN_DL_HTTP,          /* an HTTP error status (see *status) */
    AKN_DL_REFUSED,       /* a host outside the allow-list, too large, malformed response */
    AKN_DL_STORAGE        /* the file could not be written */
} akn_dl_result;

/*
 * GETs url. The body goes to mem (bounded by max_bytes) or, when file is not NULL, to that file
 * (written, synced). http_status receives the final status. why gets a sentence for the user.
 */
akn_dl_result akn_update_get(const akn_dl_config *cfg, const char *url, const char *accept, long long max_bytes,
                             akeno_buf *mem, const char *file, const akn_dl_hooks *hooks, int *http_status, char *why,
                             size_t why_len);

/* Splits "https://host[:port]/path?query"; 0 or -1. Exposed for the tests. */
int akn_update_split_url(const char *url, int *tls, char *host, size_t host_len, int *port, char *target, size_t target_len);

#endif
