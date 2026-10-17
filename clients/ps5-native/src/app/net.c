#define _POSIX_C_SOURCE 200809L

#include "net.h"

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
#include <sys/stat.h>
#include <unistd.h>

#include "akeno/akeno.h"
#include "redact.h"

#define DAEMON_REPLY_MAX 4096
#define LOOPBACK_ADDR 0x7F000001u /* 127.0.0.1 (INADDR_LOOPBACK is BSD-only in strict POSIX mode) */
#define DEVICE_RESPONSE_MAX (64 * 1024)

static int wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd p = { fd, events, 0 };
    int rc;
    do rc = poll(&p, 1, timeout_ms);
    while (rc < 0 && errno == EINTR);
    return rc;
}

/* 1 when the header block has `name: value` (case-insensitive name and value). */
static int header_has(const char *headers, const char *name, const char *value) {
    size_t nl = strlen(name), vl = strlen(value);
    for (const char *line = strstr(headers, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        const char *h = line + 2;
        if (strncasecmp(h, name, nl) != 0 || h[nl] != ':') continue;
        h += nl + 1;
        while (*h == ' ') h++;
        if (strncasecmp(h, value, vl) == 0) return 1;
    }
    return 0;
}

/* Decodes a chunked body into out; returns its length (stops at the last chunk or bad input). */
static size_t dechunk(const char *in, char *out, size_t out_len) {
    size_t used = 0;
    for (;;) {
        char *end;
        unsigned long size = strtoul(in, &end, 16);
        if (end == in || strncmp(end, "\r\n", 2) != 0 || size == 0) break;
        in = end + 2;
        if (strlen(in) < size || used + size >= out_len) break;
        memcpy(out + used, in, size);
        used += size;
        in += size;
        if (strncmp(in, "\r\n", 2) != 0) break;
        in += 2;
    }
    out[used] = '\0';
    return used;
}

void akn_net_probe_daemon(int port, int timeout_ms, akn_account_view *view) {
    view->daemon = AKN_DAEMON_NOT_RUNNING;
    view->daemon_version[0] = '\0';
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
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
        socklen_t len = sizeof(err);
        if (wait_fd(fd, POLLOUT, timeout_ms) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) rc = 0;
    }
    if (rc != 0) {
        close(fd);
        return;
    }
    char request[160];
    int n = snprintf(request, sizeof(request),
                     "GET /api/instance HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nAccept: application/json\r\nConnection: close\r\n\r\n", port);
    size_t sent = 0;
    while (n > 0 && sent < (size_t)n) {
        if (wait_fd(fd, POLLOUT, timeout_ms) <= 0) break;
        ssize_t w = send(fd, request + sent, (size_t)n - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    char reply[DAEMON_REPLY_MAX + 1];
    size_t got = 0;
    while (sent == (size_t)n && got < DAEMON_REPLY_MAX && wait_fd(fd, POLLIN, timeout_ms) > 0) {
        ssize_t r = recv(fd, reply + got, DAEMON_REPLY_MAX - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    reply[got] = '\0';
    /* Something answered on the port: it is the daemon only if it says so. */
    view->daemon = AKN_DAEMON_OTHER;
    if (strncmp(reply, "HTTP/1.1 200", 12) != 0 && strncmp(reply, "HTTP/1.0 200", 12) != 0) return;
    char *body = strstr(reply, "\r\n\r\n");
    if (!body) return;
    *body = '\0';
    body += 4;
    char decoded[DAEMON_REPLY_MAX + 1];
    size_t len = strlen(body);
    if (header_has(reply, "transfer-encoding", "chunked")) {
        len = dechunk(body, decoded, sizeof(decoded));
        body = decoded;
    }
    if (akn_parse_daemon_instance(body, len, view->daemon_version, sizeof(view->daemon_version)) == 0)
        view->daemon = AKN_DAEMON_RUNNING;
}

static void library_log(void *user, akeno_log_level level, const char *event, const char *message) {
    akn_log *log = user;
    akn_log_level l = level == AKENO_LOG_ERROR ? AKN_LOG_ERROR : level == AKENO_LOG_WARN ? AKN_LOG_WARN
                    : level == AKENO_LOG_INFO  ? AKN_LOG_INFO  : AKN_LOG_DEBUG;
    if (log) akn_logf(log, l, event, "client library: %s", message);
}

static akn_account_state state_of(akeno_status status) {
    switch (status) {
    case AKENO_OK: return AKN_ACC_SIGNED_IN;
    case AKENO_ERR_NOT_PAIRED: return AKN_ACC_NOT_PAIRED;
    case AKENO_ERR_UNAUTHORIZED: return AKN_ACC_UNAUTHORIZED;
    case AKENO_ERR_FORBIDDEN: return AKN_ACC_FORBIDDEN;
    case AKENO_ERR_NETWORK:
    case AKENO_ERR_TIMEOUT: return AKN_ACC_OFFLINE;
    case AKENO_ERR_TLS: return AKN_ACC_TLS;
    case AKENO_ERR_SERVER:
    case AKENO_ERR_PROTOCOL:
    case AKENO_ERR_TOO_LARGE: return AKN_ACC_SERVER_ERROR;
    default: return AKN_ACC_ERROR;
    }
}

void akn_net_check(const akn_platform_info *platform, akn_account_view *view, akn_log *log) {
    uint64_t started = akn_platform_now_ms();
    view->checks++;
    view->http_status = 0;
    view->error_code[0] = '\0';
    view->status_name[0] = '\0';
    view->online_id[0] = view->display_name[0] = view->device_name[0] = '\0';
    view->scope_count = 0;
    view->scopes[0] = '\0';
    view->have_skew = 0;

    akn_net_probe_daemon(platform->daemon_port, 1500, view);
    akn_logf(log, AKN_LOG_INFO, "daemon.probe", "AKENO daemon: %s%s%s", akn_daemon_state_text(view->daemon),
             view->daemon_version[0] ? " " : "", view->daemon_version);

    char settings_path[300], credential_path[300], ca_path[300], server[320];
    snprintf(settings_path, sizeof(settings_path), "%s/settings.json", platform->akeno_dir);
    snprintf(credential_path, sizeof(credential_path), "%s/device.json", platform->akeno_dir);
    snprintf(ca_path, sizeof(ca_path), "%s/ca.pem", platform->akeno_dir);
    if (akn_read_server_setting(settings_path, server, sizeof(server)) != 0) server[0] = '\0';
    view->server_desc = akn_describe_server_url(server);
    akn_inspect_credential(credential_path, view);
    akn_logf(log, AKN_LOG_INFO, "account.inputs", "server: %s; credential file: %s%s", view->server_desc,
             view->credential_present ? "present" : "missing",
             view->credential_present ? (view->credential_private ? ", private" : ", READABLE BY OTHERS") : "");

    if (!server[0]) {
        view->state = AKN_ACC_NO_SERVER;
    } else if (!view->credential_present) {
        view->state = AKN_ACC_NOT_PAIRED;
    } else {
        struct stat st;
        akeno_config config;
        memset(&config, 0, sizeof(config));
        config.base_url = server;
        config.ca_file = stat(ca_path, &st) == 0 && S_ISREG(st.st_mode) ? ca_path : NULL; /* as the daemon */
        config.credential_path = credential_path;
        config.connect_timeout_ms = 8000;
        config.io_timeout_ms = 10000;
        config.max_response_bytes = DEVICE_RESPONSE_MAX;
        config.log = library_log;
        config.log_user = log;
        config.log_level = AKENO_LOG_INFO;
        akeno_client *client = NULL;
        akeno_status status = akeno_client_create(&config, &client);
        if (status == AKENO_OK && !akeno_client_is_paired(client)) status = AKENO_ERR_NOT_PAIRED;
        if (status == AKENO_OK) {
            akeno_response response;
            akeno_response_init(&response);
            status = akeno_request(client, "GET", "/devices/current", NULL, &response);
            view->http_status = response.http_status;
            snprintf(view->error_code, sizeof(view->error_code), "%s", response.error_code);
            if (status == AKENO_OK &&
                akn_fill_device_self(akeno_response_json(&response), akn_platform_unix_time(), view) != 0)
                status = AKENO_ERR_PROTOCOL;
            akeno_response_free(&response);
        }
        akeno_client_destroy(client);
        snprintf(view->status_name, sizeof(view->status_name), "%s", akeno_status_name(status));
        view->state = state_of(status);
    }
    view->duration_ms = (unsigned)(akn_platform_now_ms() - started);
    if (view->state == AKN_ACC_SIGNED_IN)
        akn_logf(log, AKN_LOG_INFO, "account.signed_in", "signed in; %d device permissions%s; clock offset %s%llds (%u ms)",
                 view->scope_count, view->has_realtime_scope ? " incl. realtime" : "", view->have_skew ? "" : "unknown ",
                 view->have_skew ? view->clock_skew_s : 0LL, view->duration_ms);
    else
        akn_logf(log, AKN_LOG_WARN, "account.state", "%s (library %s, HTTP %d %s, %u ms)", akn_account_state_text(view->state),
                 view->status_name[0] ? view->status_name : "-", view->http_status, view->error_code, view->duration_ms);
}
