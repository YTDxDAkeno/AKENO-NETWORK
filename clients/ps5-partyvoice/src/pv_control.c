#define _POSIX_C_SOURCE 200809L
#include "pv_control.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "account.h"
#include "akeno/json.h"
#include "buf.h"
#include "net_posix.h"
#include "pv.h"
#include "pv_internal.h"
#include "pv_status.h"
#include "transport.h"
#include "ui_http.h"

#define REQUEST_MAX (UI_HTTP_MAX_HEAD + 4096)
#define BODY_MAX 4096
#define DEADLINE_MS 1500
#define LOOPBACK 0x7F000001u

int pv_control_listen(int port, int *bound_port, int *in_use) {
    *in_use = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(LOOPBACK); /* never reachable from the network */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        *in_use = errno == EADDRINUSE;
        close(fd);
        return -1;
    }
    socklen_t len = sizeof(addr);
    int flags = fcntl(fd, F_GETFL, 0);
    if (listen(fd, 8) != 0 || getsockname(fd, (struct sockaddr *)&addr, &len) != 0 || flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }
    *bound_port = ntohs(addr.sin_port);
    return fd;
}

/* ---- Answers ----------------------------------------------------------------------------------- */

static void answer_error(pv_response *r, int status, const char *code, const char *message) {
    akeno_buf b;
    akeno_buf_init(&b, sizeof(r->body) - 1);
    akeno_buf_printf(&b, "{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":", code);
    akeno_buf_append_json_string(&b, message);
    akeno_buf_append_str(&b, "}}");
    r->status = status;
    snprintf(r->body, sizeof(r->body), "%s", b.data ? b.data : "{\"ok\":false}");
    akeno_buf_free(&b);
}

static void answer_status(pv_response *r, pv_engine *e, int wrapped) {
    pv_status *s = malloc(sizeof(*s));
    akeno_buf b;
    akeno_buf_init(&b, sizeof(r->body) - 1);
    int rc = -1;
    if (s) {
        pv_engine_status(e, s);
        if (wrapped) akeno_buf_append_str(&b, "{\"ok\":true,\"status\":");
        rc = pv_status_json(s, &b);
        if (wrapped) rc |= akeno_buf_append_str(&b, "}");
        free(s);
    }
    if (rc != 0 || !b.data) {
        akeno_buf_free(&b);
        answer_error(r, 500, "STATUS_TOO_LARGE", "The status could not be written.");
        return;
    }
    r->status = 200;
    snprintf(r->body, sizeof(r->body), "%s", b.data);
    akeno_buf_free(&b);
}

/* ---- Body validation: a JSON object with exactly the allowed members ------------------------- */

typedef struct {
    const char *name;
    akeno_json_type type;
    long long min, max; /* numbers: integers within */
    int required;
} member_rule;

/* 0 when root matches the rules; else an error was answered. */
static int check_body(const akeno_json *root, const member_rule *rules, int n, pv_response *r) {
    if (!root || root->type != AKENO_JSON_OBJECT) {
        answer_error(r, 400, "INVALID_BODY", "Send a JSON object.");
        return -1;
    }
    for (const akeno_json *m = root->child; m; m = m->next) {
        int known = 0;
        for (int i = 0; i < n && !known; i++) {
            if (!m->key || strcmp(m->key, rules[i].name) != 0) continue;
            known = 1;
            if (m->type != rules[i].type) {
                answer_error(r, 400, "INVALID_BODY", "A member has the wrong type.");
                return -1;
            }
            if (m->type == AKENO_JSON_NUMBER) {
                int64_t v;
                if (akeno_json_int64(m, &v) != 0 || v < rules[i].min || v > rules[i].max) {
                    answer_error(r, 400, "OUT_OF_RANGE", "A value is outside its range.");
                    return -1;
                }
            }
        }
        if (!known) {
            answer_error(r, 400, "INVALID_BODY", "The request has a member this interface does not know.");
            return -1;
        }
    }
    for (int i = 0; i < n; i++) {
        if (rules[i].required && !akeno_json_get(root, rules[i].name)) {
            answer_error(r, 400, "INVALID_BODY", "A required member is missing.");
            return -1;
        }
    }
    return 0;
}

static int get_int(const akeno_json *root, const char *key, int fallback) {
    int64_t v;
    return akeno_json_int64(akeno_json_get(root, key), &v) == 0 ? (int)v : fallback;
}

static void answer_engine(pv_response *r, pv_engine *e, int rc, const pv_error *err) {
    if (rc == 0) answer_status(r, e, 1);
    else answer_error(r, 409, err->code, err->message);
}

static int key_ok(const pv_control_config *c, const char *given) {
    char path[300], key[AKN_NATIVE_KEY_LEN + 1];
    snprintf(path, sizeof(path), "%s/native.key", c->akeno_dir);
    if (!given || !given[0] || akn_read_native_key(path, key, sizeof(key)) != 0) return 0;
    int ok = ui_http_secret_equal(given, key);
    memset(key, 0, sizeof(key));
    return ok;
}

void pv_control_handle(pv_engine *e, const pv_control_config *c, const char *data, size_t len, pv_response *r) {
    memset(r, 0, sizeof(*r));
    ui_request req;
    int parsed = ui_http_parse_request(data, len, &req);
    if (parsed != UI_PARSE_OK) {
        answer_error(r, parsed == UI_PARSE_TOO_LARGE ? 413 : 400, "BAD_REQUEST", "Malformed request.");
        return;
    }
    if (!ui_http_host_allowed(req.host, c->port)) {
        answer_error(r, 400, "BAD_HOST", "Use 127.0.0.1.");
        return;
    }
    if (req.origin[0]) { /* a web page: never */
        answer_error(r, 403, "ORIGIN_REFUSED", "Web pages may not use the voice service.");
        return;
    }
    const char *body = data + req.head_len;
    size_t body_len = len - req.head_len;
    if (req.content_length >= 0 && (size_t)req.content_length < body_len) body_len = (size_t)req.content_length;
    if (body_len > BODY_MAX) {
        answer_error(r, 413, "TOO_LARGE", "The request is too large.");
        return;
    }
    int get = strcmp(req.method, "GET") == 0, post = strcmp(req.method, "POST") == 0;
    if (get && strcmp(req.path, "/api/voice/hello") == 0) {
        r->status = 200;
        snprintf(r->body, sizeof(r->body), "{\"ok\":true,\"app\":\"%s\",\"version\":\"%s\",\"build\":\"%s\",\"protocol\":%d}", PV_APP, PV_VERSION,
                 PV_BUILD_ID, PV_API_PROTOCOL);
        return;
    }
    if (strncmp(req.path, "/api/voice/", 11) != 0) {
        answer_error(r, 404, "NOT_FOUND", "Not found.");
        return;
    }
    if (!key_ok(c, req.native_key)) {
        answer_error(r, 403, "NATIVE_KEY_INVALID", "This key is not the AKENO daemon's current native key.");
        return;
    }
    if (get && strcmp(req.path, "/api/voice/status") == 0) {
        answer_status(r, e, 0);
        return;
    }
    if (!post) {
        answer_error(r, 405, "METHOD_NOT_ALLOWED", "Use GET for status and POST for changes.");
        return;
    }
    if (body_len > 0 && strncmp(req.content_type, "application/json", 16) != 0) {
        answer_error(r, 415, "JSON_REQUIRED", "Send JSON.");
        return;
    }
    akeno_json_doc *doc = akeno_json_parse(body_len ? body : "{}", body_len ? body_len : 2, NULL, 0);
    const akeno_json *root = akeno_json_root(doc);
    pv_error err;
    memset(&err, 0, sizeof(err));
    const char *p = req.path + 11;
    if (strcmp(p, "join") == 0) {
        static const member_rule rules[] = { { "partyId", AKENO_JSON_STRING, 0, 0, 1 } };
        if (check_body(root, rules, 1, r) == 0) {
            const char *id = akeno_json_get_string(root, "partyId", "");
            if (!pv_party_id_ok(id)) answer_error(r, 400, "INVALID_PARTY", "That is not a party ID.");
            else answer_engine(r, e, pv_engine_join(e, id, &err), &err);
        }
    } else if (strcmp(p, "leave") == 0) {
        if (check_body(root, NULL, 0, r) == 0) {
            pv_engine_leave(e, "You left the voice chat.");
            answer_status(r, e, 1);
        }
    } else if (strcmp(p, "mic") == 0) {
        static const member_rule rules[] = { { "enabled", AKENO_JSON_BOOL, 0, 0, 1 } };
        if (check_body(root, rules, 1, r) == 0) answer_engine(r, e, pv_engine_set_mic(e, akeno_json_get_bool(root, "enabled", 0), &err), &err);
    } else if (strcmp(p, "mute") == 0) {
        static const member_rule rules[] = { { "muted", AKENO_JSON_BOOL, 0, 0, 1 } };
        if (check_body(root, rules, 1, r) == 0) answer_engine(r, e, pv_engine_set_mute(e, akeno_json_get_bool(root, "muted", 0), &err), &err);
    } else if (strcmp(p, "deafen") == 0) {
        static const member_rule rules[] = { { "deafened", AKENO_JSON_BOOL, 0, 0, 1 } };
        if (check_body(root, rules, 1, r) == 0) {
            pv_engine_set_deafen(e, akeno_json_get_bool(root, "deafened", 0));
            answer_status(r, e, 1);
        }
    } else if (strcmp(p, "volume") == 0) {
        static const member_rule rules[] = { { "output", AKENO_JSON_NUMBER, PV_VOLUME_MIN, PV_VOLUME_MAX, 0 },
                                             { "mic", AKENO_JSON_NUMBER, PV_GAIN_MIN, PV_GAIN_MAX, 0 } };
        if (check_body(root, rules, 2, r) == 0) {
            if (!root->child) {
                answer_error(r, 400, "INVALID_BODY", "Send output and/or mic.");
            } else {
                if (akeno_json_get(root, "output")) pv_engine_set_volume(e, get_int(root, "output", 100));
                if (akeno_json_get(root, "mic")) pv_engine_set_gain(e, get_int(root, "mic", 100));
                answer_status(r, e, 1);
            }
        }
    } else if (strcmp(p, "route") == 0) {
        static const member_rule rules[] = { { "route", AKENO_JSON_NUMBER, 1, PV_ROUTES, 1 } };
        if (check_body(root, rules, 1, r) == 0) answer_engine(r, e, pv_engine_set_route(e, get_int(root, "route", 1), &err), &err);
    } else if (strcmp(p, "test-sound") == 0) {
        if (check_body(root, NULL, 0, r) == 0) answer_engine(r, e, pv_engine_test_sound(e, &err), &err);
    } else if (strcmp(p, "exit") == 0) {
        if (check_body(root, NULL, 0, r) == 0) {
            r->exit_requested = 1;
            r->status = 200;
            snprintf(r->body, sizeof(r->body), "{\"ok\":true,\"stopping\":true}");
        }
    } else {
        answer_error(r, 404, "NOT_FOUND", "Not found.");
    }
    akeno_json_free(doc);
}

/* ---- Sockets ----------------------------------------------------------------------------------- */

static void serve_one(int fd, pv_engine *e, const pv_control_config *c, int *exit_requested) {
    char *buf = malloc(REQUEST_MAX + 1);
    pv_response *r = malloc(sizeof(*r));
    if (!buf || !r) {
        free(buf);
        free(r);
        return;
    }
    long long deadline = akeno_now_ms() + DEADLINE_MS;
    size_t got = 0;
    int complete = 0;
    while (got < REQUEST_MAX) {
        if (akeno_wait_fd(fd, POLLIN, akeno_remaining_ms(deadline)) <= 0) break;
        ssize_t n = recv(fd, buf + got, REQUEST_MAX - got, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) break;
        got += (size_t)n;
        ui_request req;
        int parsed = ui_http_parse_request(buf, got, &req);
        if (parsed == UI_PARSE_INCOMPLETE) continue;
        if (parsed != UI_PARSE_OK) {
            complete = 1;
            break;
        }
        size_t want = req.head_len + (req.content_length > 0 ? (size_t)req.content_length : 0);
        if (req.content_length > BODY_MAX || got >= want) {
            complete = 1;
            break;
        }
    }
    if (complete || got) {
        buf[got] = '\0';
        pv_control_handle(e, c, buf, got, r);
        char head[200];
        size_t blen = strlen(r->body);
        int hn = snprintf(head, sizeof(head),
                          "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                          r->status ? r->status : 500, r->status == 200 ? "OK" : "Error", blen);
        if (akeno_send_nosignal(fd, head, (size_t)hn) == hn) {
            size_t sent = 0;
            while (sent < blen) {
                if (akeno_wait_fd(fd, POLLOUT, akeno_remaining_ms(deadline)) <= 0) break;
                long w = akeno_send_nosignal(fd, r->body + sent, blen - sent);
                if (w <= 0) break;
                sent += (size_t)w;
            }
        }
        if (r->exit_requested) *exit_requested = 1;
    }
    free(buf);
    free(r);
}

int pv_control_serve(int listen_fd, pv_engine *e, const pv_control_config *c, int wait_ms) {
    int exit_requested = 0;
    struct pollfd p = { listen_fd, POLLIN, 0 };
    if (poll(&p, 1, wait_ms) <= 0) return 0;
    for (int i = 0; i < 8; i++) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) break;
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        serve_one(fd, e, c, &exit_requested);
        close(fd);
    }
    return exit_requested;
}
