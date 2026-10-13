#define _POSIX_C_SOURCE 200809L
#include "client_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "http.h"
#include "transport.h"

#define CREDENTIAL_MAX_BYTES 8192

const char *akeno_status_name(akeno_status status) {
    switch (status) {
    case AKENO_OK: return "ok";
    case AKENO_ERR_ARGUMENT: return "invalid-argument";
    case AKENO_ERR_NO_MEMORY: return "out-of-memory";
    case AKENO_ERR_INSECURE_URL: return "insecure-url";
    case AKENO_ERR_NETWORK: return "network-error";
    case AKENO_ERR_TIMEOUT: return "timeout";
    case AKENO_ERR_TLS: return "tls-error";
    case AKENO_ERR_PROTOCOL: return "protocol-error";
    case AKENO_ERR_TOO_LARGE: return "response-too-large";
    case AKENO_ERR_NOT_PAIRED: return "not-paired";
    case AKENO_ERR_UNAUTHORIZED: return "unauthorized";
    case AKENO_ERR_FORBIDDEN: return "forbidden";
    case AKENO_ERR_NOT_FOUND: return "not-found";
    case AKENO_ERR_RATE_LIMITED: return "rate-limited";
    case AKENO_ERR_HTTP: return "http-error";
    case AKENO_ERR_SERVER: return "server-error";
    case AKENO_ERR_PAIRING_DENIED: return "pairing-denied";
    case AKENO_ERR_PAIRING_EXPIRED: return "pairing-expired";
    case AKENO_ERR_PAIRING_INVALID: return "pairing-invalid";
    case AKENO_ERR_STORAGE: return "storage-error";
    case AKENO_ERR_STOPPED: return "stopped";
    case AKENO_ERR_GAVE_UP: return "gave-up";
    }
    return "unknown";
}

akeno_status akeno_global_init(void) { return akeno_transport_init(); }
void akeno_global_cleanup(void) { akeno_transport_cleanup(); }

static int token_char(char c) {
    return c == '-' || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Copies text into out (bounded), replacing anything that looks like a device token or code. */
void akeno_redact(const char *text, char *out, size_t out_len) {
    size_t o = 0;
    if (out_len == 0) return;
    for (const char *p = text; *p && o + 1 < out_len;) {
        if ((strncmp(p, "akd_", 4) == 0 || strncmp(p, "akp_", 4) == 0) && token_char(p[4])) {
            const char *mark = p[2] == 'd' ? "akd_[redacted]" : "akp_[redacted]";
            for (const char *m = mark; *m && o + 1 < out_len; m++) out[o++] = *m;
            p += 4;
            while (token_char(*p)) p++;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
}

void akeno_log(const akeno_client *client, akeno_log_level level, const char *event, const char *fmt, ...) {
    if (!client->log || level < client->log_level) return;
    char raw[512];
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(raw, sizeof(raw), fmt, ap);
    va_end(ap);
    akeno_redact(raw, message, sizeof(message));
    client->log(client->log_user, level, event, message);
}

static char *dup_or_null(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

static int valid_token(const char *token, const char *prefix) {
    if (strncmp(token, prefix, 4) != 0 || strlen(token) != 47) return 0;
    for (const char *p = token + 4; *p; p++)
        if (!token_char(*p)) return 0;
    return 1;
}

static void copy_bounded(char *dst, size_t dst_len, const char *src) {
    snprintf(dst, dst_len, "%s", src ? src : "");
}

/* ---- Credential storage --------------------------------------------------------------------- */

static void clear_credentials(akeno_client *client) {
    memset(client->token, 0, sizeof(client->token));
    client->device_id[0] = '\0';
    client->online_id[0] = '\0';
    client->paired = 0;
}

static akeno_status load_credentials(akeno_client *client) {
    clear_credentials(client);
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(client->credential_path, flags);
    if (fd < 0) return errno == ENOENT ? AKENO_ERR_NOT_PAIRED : AKENO_ERR_STORAGE;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > CREDENTIAL_MAX_BYTES) {
        close(fd);
        akeno_log(client, AKENO_LOG_ERROR, "credentials.invalid", "The credential file is not a regular file of a sane size.");
        return AKENO_ERR_STORAGE;
    }
    if (st.st_mode & 077) {
        close(fd);
        akeno_log(client, AKENO_LOG_ERROR, "credentials.permissions",
                  "The credential file can be read by other users. Delete it and pair again.");
        return AKENO_ERR_STORAGE;
    }
    char data[CREDENTIAL_MAX_BYTES];
    ssize_t n = read(fd, data, sizeof(data));
    close(fd);
    if (n <= 0) return AKENO_ERR_STORAGE;
    akeno_json_doc *doc = akeno_json_parse(data, (size_t)n, NULL, 0);
    memset(data, 0, sizeof(data));
    if (!doc) return AKENO_ERR_STORAGE;
    const akeno_json *root = akeno_json_root(doc);
    const char *server = akeno_json_get_string(root, "server", "");
    const char *token = akeno_json_get_string(root, "accessToken", "");
    akeno_status status = AKENO_ERR_STORAGE;
    if (strcmp(server, client->server_key) != 0) {
        /* Never send a token to a different server than the one it was issued by. */
        akeno_log(client, AKENO_LOG_WARN, "credentials.other_server",
                  "The stored credential belongs to a different server (%s). Pair again for this server.", server);
        status = AKENO_ERR_NOT_PAIRED;
    } else if (valid_token(token, "akd_")) {
        copy_bounded(client->token, sizeof(client->token), token);
        copy_bounded(client->device_id, sizeof(client->device_id), akeno_json_get_string(root, "deviceId", ""));
        copy_bounded(client->online_id, sizeof(client->online_id), akeno_json_get_string(root, "onlineId", ""));
        client->paired = 1;
        status = AKENO_OK;
    }
    akeno_json_free(doc);
    return status;
}

static akeno_status save_credentials(akeno_client *client) {
    akeno_buf out;
    akeno_buf_init(&out, CREDENTIAL_MAX_BYTES);
    int rc = akeno_buf_append_str(&out, "{\"version\":1,\"server\":");
    rc |= akeno_buf_append_json_string(&out, client->server_key);
    rc |= akeno_buf_append_str(&out, ",\"accessToken\":");
    rc |= akeno_buf_append_json_string(&out, client->token);
    rc |= akeno_buf_append_str(&out, ",\"deviceId\":");
    rc |= akeno_buf_append_json_string(&out, client->device_id);
    rc |= akeno_buf_append_str(&out, ",\"onlineId\":");
    rc |= akeno_buf_append_json_string(&out, client->online_id);
    rc |= akeno_buf_append_str(&out, "}\n");
    if (rc != 0) {
        akeno_buf_free(&out);
        return AKENO_ERR_NO_MEMORY;
    }
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", client->credential_path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    akeno_status status = AKENO_ERR_STORAGE;
    if (fd >= 0) {
        ssize_t written = write(fd, out.data, out.len);
        int ok = written == (ssize_t)out.len && fsync(fd) == 0;
        ok = close(fd) == 0 && ok;
        if (ok && rename(tmp, client->credential_path) == 0) status = AKENO_OK;
        else unlink(tmp);
    }
    if (status != AKENO_OK)
        akeno_log(client, AKENO_LOG_ERROR, "credentials.write_failed", "Could not write the credential file: %s", strerror(errno));
    akeno_buf_free(&out);
    return status;
}

akeno_status akeno_forget_credentials(akeno_client *client) {
    clear_credentials(client);
    if (unlink(client->credential_path) != 0 && errno != ENOENT) return AKENO_ERR_STORAGE;
    return AKENO_OK;
}

/* ---- Client lifecycle ----------------------------------------------------------------------- */

akeno_status akeno_client_create(const akeno_config *config, akeno_client **out) {
    *out = NULL;
    if (!config || !config->base_url || !config->credential_path || strlen(config->credential_path) > 1024)
        return AKENO_ERR_ARGUMENT;
    akeno_client *client = calloc(1, sizeof(*client));
    if (!client) return AKENO_ERR_NO_MEMORY;
    client->log = config->log;
    client->log_user = config->log_user;
    client->log_level = config->log_level;
    if (akeno_url_parse(config->base_url, &client->url) != 0) {
        free(client);
        return AKENO_ERR_ARGUMENT;
    }
    if (!client->url.tls && !akeno_url_is_loopback(&client->url)) {
        akeno_log(client, AKENO_LOG_ERROR, "config.insecure_url",
                  "Refusing plain HTTP to %s. Use https:// (plain HTTP is only allowed for loopback testing).", client->url.host);
        free(client);
        return AKENO_ERR_INSECURE_URL;
    }
    int bracket = strchr(client->url.host, ':') != NULL;
    snprintf(client->server_key, sizeof(client->server_key), "%s://%s%s%s:%d%s", client->url.tls ? "https" : "http",
             bracket ? "[" : "", client->url.host, bracket ? "]" : "", client->url.port, client->url.base_path);
    client->ca_file = dup_or_null(config->ca_file);
    client->credential_path = dup_or_null(config->credential_path);
    if ((config->ca_file && !client->ca_file) || !client->credential_path) {
        akeno_client_destroy(client);
        return AKENO_ERR_NO_MEMORY;
    }
    client->connect_timeout_ms = config->connect_timeout_ms > 0 ? config->connect_timeout_ms : 10000;
    client->io_timeout_ms = config->io_timeout_ms > 0 ? config->io_timeout_ms : 15000;
    client->max_response_bytes = config->max_response_bytes > 0 ? config->max_response_bytes : AKENO_BUF_DEFAULT_LIMIT;

    akeno_status loaded = load_credentials(client);
    if (loaded == AKENO_ERR_STORAGE) {
        akeno_log(client, AKENO_LOG_WARN, "credentials.unreadable", "The stored credential could not be used; pair this device again.");
    }
    *out = client;
    return AKENO_OK;
}

void akeno_client_destroy(akeno_client *client) {
    if (!client) return;
    clear_credentials(client);
    free(client->ca_file);
    free(client->credential_path);
    free(client);
}

int akeno_client_is_paired(const akeno_client *client) { return client->paired; }

const char *akeno_client_online_id(const akeno_client *client) {
    return client->paired && client->online_id[0] ? client->online_id : NULL;
}

/* ---- Requests ------------------------------------------------------------------------------- */

void akeno_response_init(akeno_response *response) { memset(response, 0, sizeof(*response)); }

void akeno_response_free(akeno_response *response) {
    if (!response) return;
    akeno_json_free(response->doc);
    memset(response, 0, sizeof(*response));
}

const akeno_json *akeno_response_json(const akeno_response *response) { return akeno_json_root(response->doc); }

static int safe_path(const char *path) {
    if (!path || path[0] != '/' || strlen(path) > 512) return 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p <= 0x20 || *p >= 0x7F) return 0; /* Prevents header injection. */
    return 1;
}

int akeno_build_host_header(const akeno_client *client, char *out, size_t out_len) {
    int bracket = strchr(client->url.host, ':') != NULL;
    int default_port = client->url.port == (client->url.tls ? 443 : 80);
    char port[8] = "";
    if (!default_port) snprintf(port, sizeof(port), ":%d", client->url.port);
    int n = snprintf(out, out_len, "%s%s%s%s", bracket ? "[" : "", client->url.host, bracket ? "]" : "", port);
    return n > 0 && (size_t)n < out_len ? 0 : -1;
}

static akeno_status map_http_status(int status) {
    if (status >= 200 && status < 300) return AKENO_OK;
    switch (status) {
    case 401: return AKENO_ERR_UNAUTHORIZED;
    case 403: return AKENO_ERR_FORBIDDEN;
    case 404: return AKENO_ERR_NOT_FOUND;
    case 429: return AKENO_ERR_RATE_LIMITED;
    default: return status >= 500 ? AKENO_ERR_SERVER : AKENO_ERR_HTTP;
    }
}

akeno_status akeno_request_raw(akeno_client *client, const char *method, const char *path, const char *json_body,
                               int with_token, akeno_response *response) {
    akeno_response_init(response);
    /* Only the methods the API uses; the method token is written into the request line. */
    if (!client || !safe_path(path) || !method ||
        (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0 && strcmp(method, "PUT") != 0 &&
         strcmp(method, "PATCH") != 0 && strcmp(method, "DELETE") != 0))
        return AKENO_ERR_ARGUMENT;
    if (with_token && !client->paired) return AKENO_ERR_NOT_PAIRED;

    char host[300];
    if (akeno_build_host_header(client, host, sizeof(host)) != 0) return AKENO_ERR_ARGUMENT;
    int has_body = json_body != NULL || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0;
    const char *body = json_body ? json_body : "{}";

    akeno_buf request;
    akeno_buf_init(&request, 128 * 1024);
    int rc = akeno_buf_printf(&request,
                              "%s %s/api/v1%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: akeno-device-client/" AKENO_CLIENT_VERSION
                              "\r\nAccept: application/json\r\nConnection: close\r\n",
                              method, client->url.base_path, path, host);
    if (rc == 0 && with_token) rc = akeno_buf_printf(&request, "Authorization: Bearer %s\r\n", client->token);
    if (rc == 0 && has_body)
        rc = akeno_buf_printf(&request, "Content-Type: application/json\r\nContent-Length: %zu\r\n", strlen(body));
    if (rc == 0) rc = akeno_buf_append_str(&request, "\r\n");
    if (rc == 0 && has_body) rc = akeno_buf_append_str(&request, body);
    if (rc != 0) {
        akeno_buf_free(&request);
        return AKENO_ERR_TOO_LARGE;
    }

    char err[256] = "";
    akeno_conn *conn = NULL;
    akeno_status status = akeno_conn_open(&client->url, client->ca_file, client->connect_timeout_ms, &conn, err, sizeof(err));
    if (status != AKENO_OK) {
        akeno_log(client, AKENO_LOG_WARN, "http.connect_failed", "%s", err);
        akeno_buf_free(&request);
        return status;
    }
    akeno_http_head head;
    akeno_buf body_out;
    akeno_buf_init(&body_out, client->max_response_bytes);
    status = akeno_http_exchange(conn, &request, client->io_timeout_ms, &head, &body_out);
    akeno_conn_close(conn);
    akeno_buf_free(&request);
    if (status != AKENO_OK) {
        akeno_log(client, AKENO_LOG_WARN, "http.exchange_failed", "%s %s failed: %s", method, path, akeno_status_name(status));
        akeno_buf_free(&body_out);
        return status;
    }

    response->http_status = head.status;
    if (body_out.len > 0) {
        char perr[96];
        response->doc = akeno_json_parse(body_out.data, body_out.len, perr, sizeof(perr));
        if (!response->doc) {
            akeno_log(client, AKENO_LOG_WARN, "http.invalid_json", "%s %s returned HTTP %d with an invalid JSON body (%s).",
                      method, path, head.status, perr);
            akeno_buf_free(&body_out);
            return AKENO_ERR_PROTOCOL;
        }
    }
    akeno_buf_free(&body_out);

    const akeno_json *error = akeno_json_get(akeno_response_json(response), "error");
    copy_bounded(response->error_code, sizeof(response->error_code), akeno_json_get_string(error, "code", ""));
    copy_bounded(response->error_message, sizeof(response->error_message), akeno_json_get_string(error, "message", ""));
    status = map_http_status(head.status);
    akeno_log(client, status == AKENO_OK ? AKENO_LOG_DEBUG : AKENO_LOG_INFO, "http.response", "%s %s -> %d%s%s", method, path,
              head.status, response->error_code[0] ? " " : "", response->error_code);
    return status;
}

akeno_status akeno_request(akeno_client *client, const char *method, const char *path, const char *json_body,
                           akeno_response *response) {
    akeno_status status = akeno_request_raw(client, method, path, json_body, 1, response);
    if (status == AKENO_ERR_UNAUTHORIZED) {
        akeno_log(client, AKENO_LOG_WARN, "auth.unpaired", "The server no longer accepts this device. Pair it again.");
        clear_credentials(client);
    }
    return status;
}

/* ---- Pairing -------------------------------------------------------------------------------- */

akeno_status akeno_pairing_start(akeno_client *client, const char *device_name, const char *platform,
                                 akeno_pairing *pairing, akeno_response *response) {
    memset(pairing, 0, sizeof(*pairing));
    if (!device_name || !*device_name) return AKENO_ERR_ARGUMENT;
    akeno_buf body;
    akeno_buf_init(&body, 4096);
    int rc = akeno_buf_append_str(&body, "{\"deviceName\":");
    rc |= akeno_buf_append_json_string(&body, device_name);
    if (platform && *platform) {
        rc |= akeno_buf_append_str(&body, ",\"platform\":");
        rc |= akeno_buf_append_json_string(&body, platform);
    }
    rc |= akeno_buf_append_str(&body, ",\"clientVersion\":\"" AKENO_CLIENT_VERSION "\"}");
    if (rc != 0) {
        akeno_buf_free(&body);
        return AKENO_ERR_NO_MEMORY;
    }
    akeno_status status = akeno_request_raw(client, "POST", "/devices/pairings", body.data, 0, response);
    akeno_buf_free(&body);
    if (status != AKENO_OK) return status;

    const akeno_json *root = akeno_response_json(response);
    const char *device_code = akeno_json_get_string(root, "deviceCode", "");
    const char *user_code = akeno_json_get_string(root, "userCode", "");
    const akeno_json *expires = akeno_json_get(root, "expiresIn");
    const akeno_json *interval = akeno_json_get(root, "interval");
    int64_t expires_in = 0, interval_s = 0;
    if (!valid_token(device_code, "akp_") || strlen(user_code) != 9 || akeno_json_int64(expires, &expires_in) != 0 ||
        akeno_json_int64(interval, &interval_s) != 0 || expires_in <= 0 || expires_in > 3600 || interval_s < 1 || interval_s > 60) {
        akeno_log(client, AKENO_LOG_ERROR, "pairing.invalid_response", "The server returned an unexpected pairing response.");
        return AKENO_ERR_PROTOCOL;
    }
    copy_bounded(pairing->device_code, sizeof(pairing->device_code), device_code);
    copy_bounded(pairing->user_code, sizeof(pairing->user_code), user_code);
    copy_bounded(pairing->verification_uri, sizeof(pairing->verification_uri), akeno_json_get_string(root, "verificationUri", ""));
    copy_bounded(pairing->verification_uri_complete, sizeof(pairing->verification_uri_complete),
                 akeno_json_get_string(root, "verificationUriComplete", ""));
    pairing->expires_in = (int)expires_in;
    pairing->interval = (int)interval_s;
    return AKENO_OK;
}

static int sleep_interruptible(int ms, akeno_should_stop_fn should_stop, void *user) {
    long long until = akeno_now_ms() + ms;
    while (akeno_now_ms() < until) {
        if (should_stop && should_stop(user)) return 1;
        long long left = until - akeno_now_ms();
        akeno_sleep_ms(left > 200 ? 200 : (int)left);
    }
    return should_stop && should_stop(user);
}

akeno_status akeno_pairing_wait(akeno_client *client, const akeno_pairing *pairing, akeno_should_stop_fn should_stop,
                                void *user) {
    long long deadline = akeno_now_ms() + (long long)pairing->expires_in * 1000;
    int interval_ms = pairing->interval * 1000;
    char body[96];
    snprintf(body, sizeof(body), "{\"deviceCode\":\"%s\"}", pairing->device_code);
    int network_failures = 0;
    for (;;) {
        if (sleep_interruptible(interval_ms, should_stop, user)) return AKENO_ERR_STOPPED;
        if (akeno_now_ms() > deadline) return AKENO_ERR_PAIRING_EXPIRED;
        akeno_response response;
        akeno_status status = akeno_request_raw(client, "POST", "/devices/pairings/token", body, 0, &response);
        if (status == AKENO_OK) {
            const akeno_json *root = akeno_response_json(&response);
            const char *token = akeno_json_get_string(root, "accessToken", "");
            if (!valid_token(token, "akd_")) {
                akeno_response_free(&response);
                return AKENO_ERR_PROTOCOL;
            }
            copy_bounded(client->token, sizeof(client->token), token);
            copy_bounded(client->device_id, sizeof(client->device_id), akeno_json_get_string(akeno_json_get(root, "device"), "id", ""));
            copy_bounded(client->online_id, sizeof(client->online_id), akeno_json_get_string(akeno_json_get(root, "user"), "onlineId", ""));
            client->paired = 1;
            akeno_response_free(&response);
            akeno_status saved = save_credentials(client);
            if (saved != AKENO_OK) return saved;
            akeno_log(client, AKENO_LOG_INFO, "pairing.completed", "Paired as %s.", client->online_id);
            return AKENO_OK;
        }
        const char *code = response.error_code;
        akeno_status result = AKENO_OK;
        if (strcmp(code, "AUTHORIZATION_PENDING") == 0) {
            network_failures = 0;
        } else if (strcmp(code, "SLOW_DOWN") == 0) {
            interval_ms += 5000; /* RFC 8628 section 3.5 */
        } else if (strcmp(code, "ACCESS_DENIED") == 0) {
            result = AKENO_ERR_PAIRING_DENIED;
        } else if (strcmp(code, "EXPIRED_TOKEN") == 0) {
            result = AKENO_ERR_PAIRING_EXPIRED;
        } else if (strcmp(code, "PAIRING_INVALID") == 0) {
            result = AKENO_ERR_PAIRING_INVALID;
        } else if (status == AKENO_ERR_NETWORK || status == AKENO_ERR_TIMEOUT || status == AKENO_ERR_SERVER ||
                   status == AKENO_ERR_RATE_LIMITED) {
            /* Server restart or network glitch: keep polling until the code expires, a bit slower. */
            network_failures++;
            akeno_log(client, AKENO_LOG_WARN, "pairing.poll_failed", "Polling failed (%s); retrying.", akeno_status_name(status));
            if (network_failures > 3 && interval_ms < 30000) interval_ms *= 2;
        } else {
            result = status;
        }
        akeno_response_free(&response);
        if (result != AKENO_OK) return result;
    }
}

akeno_status akeno_logout(akeno_client *client) {
    if (client->paired) {
        akeno_response response;
        akeno_status status = akeno_request_raw(client, "DELETE", "/devices/current", NULL, 1, &response);
        akeno_response_free(&response);
        if (status != AKENO_OK && status != AKENO_ERR_UNAUTHORIZED) {
            akeno_log(client, AKENO_LOG_WARN, "logout.server_unreachable",
                      "Could not reach the server to revoke this device (%s). Unpair it on the website too.", akeno_status_name(status));
        }
    }
    return akeno_forget_credentials(client);
}
