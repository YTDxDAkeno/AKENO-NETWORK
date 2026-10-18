#include "actions.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "akeno/json.h"

void akn_companion_random16(unsigned char out[16]) {
    int fd = open("/dev/urandom", O_RDONLY);
    size_t got = 0;
    if (fd >= 0) {
        while (got < 16) {
            ssize_t n = read(fd, out + got, 16 - got);
            if (n <= 0) break;
            got += (size_t)n;
        }
        close(fd);
    }
    if (got == 16) return;
    /* splitmix64 over both clocks and a counter */
    static uint64_t counter;
    struct timespec a, b;
    clock_gettime(CLOCK_REALTIME, &a);
    clock_gettime(CLOCK_MONOTONIC, &b);
    uint64_t x = ((uint64_t)a.tv_sec << 32) ^ (uint64_t)a.tv_nsec ^ ((uint64_t)b.tv_nsec << 20) ^ (++counter * 0x9E3779B97F4A7C15ull);
    for (int i = 0; i < 16; i += 8) {
        x += 0x9E3779B97F4A7C15ull;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        memcpy(out + i, &z, 8);
    }
}

static const struct {
    const char *prefix, *verb;
    int with_id;
} ROUTES[] = {
    { "refresh", "refresh", 0 }, { "presence", "presence", 0 }, { "pcreate", "pcreate", 0 }, { "nread", "nread", 0 },
    { "reply.", "reply", 1 },    { "dm.", "dm", 1 },            { "read.", "read", 1 },      { "pinv.", "pinv", 1 },
    { "fpinv.", "pinv", 1 },     { "pjoin.", "pjoin", 1 },      { "fpjoin.", "pjoin", 1 },   { "pleave.", "pleave", 1 },
    { "iacc.", "iacc", 1 },      { "idec.", "idec", 1 },        { "racc.", "racc", 1 },      { "rdec.", "rdec", 1 },
};

const char *akn_companion_action_verb(const char *node_id, const char **item_id) {
    if (item_id) *item_id = "";
    for (size_t i = 0; node_id && i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        size_t n = strlen(ROUTES[i].prefix);
        if (!ROUTES[i].with_id) {
            if (strcmp(node_id, ROUTES[i].prefix) == 0) return ROUTES[i].verb;
        } else if (strncmp(node_id, ROUTES[i].prefix, n) == 0 && akn_companion_id_ok(node_id + n)) {
            if (item_id) *item_id = node_id + n;
            return ROUTES[i].verb;
        }
    }
    return NULL;
}

/* "reply.<id>" -> the ID when the prefix matches and the ID is a valid server ID. */
static const char *after_prefix(const char *node_id, const char *prefix) {
    size_t n = strlen(prefix);
    if (strncmp(node_id, prefix, n) != 0) return NULL;
    return akn_companion_id_ok(node_id + n) ? node_id + n : NULL;
}

/* Trims spaces and line breaks around a typed text into out. */
static void trimmed(const char *in, char *out, size_t len) {
    while (*in == ' ' || *in == '\t' || *in == '\n' || *in == '\r') in++;
    size_t n = strlen(in);
    while (n > 0 && (in[n - 1] == ' ' || in[n - 1] == '\t' || in[n - 1] == '\n' || in[n - 1] == '\r')) n--;
    if (n >= len) n = len - 1;
    memcpy(out, in, n);
    out[n] = '\0';
}

__attribute__((format(printf, 4, 5))) static void done(akn_action_result *out, int ok, unsigned reload, const char *fmt, ...) {
    out->ok = ok;
    out->reload |= reload;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out->message, sizeof(out->message), fmt, ap);
    va_end(ap);
}

static void failed(akn_action_result *out, const char *what, const char *err) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message), "%s: %s", what, err[0] ? err : "it did not work.");
}

static int send_message(akn_daemon_link *link, const char *conv_id, const char *text, char *err, size_t err_len) {
    unsigned char rnd[16];
    akn_companion_random16(rnd);
    char client_id[37], body[AKN_UI_REPLY_MAX * 6 + 128], path[160];
    akn_uuid_v4(rnd, client_id);
    if (akn_message_body_json(text, client_id, body, sizeof(body)) != 0) {
        snprintf(err, err_len, "The message cannot be sent as it is.");
        return -1;
    }
    snprintf(path, sizeof(path), "/api/v1/conversations/%s/messages", conv_id);
    return akn_daemon_write(link, "POST", path, body, err, err_len, NULL, NULL);
}

static int typed_message(const char *value, char *text, size_t len, akn_action_result *out) {
    trimmed(value ? value : "", text, len);
    if (!text[0]) {
        done(out, 0, 0, "Nothing to send: the message was empty.");
        return -1;
    }
    if (!akn_message_valid(text)) {
        done(out, 0, 0, "This message cannot be sent (too long or not plain text).");
        return -1;
    }
    return 0;
}

/* POST /api/v1/<what>/<id>/<verb>; ok_message on success, "<what failed>: <reason>" otherwise. */
static void post_simple(akn_daemon_link *link, akn_action_result *out, const char *collection, const char *id, const char *verb, const char *body,
                        unsigned reload, const char *ok_message, const char *what_failed) {
    char path[200], err[200] = "";
    snprintf(path, sizeof(path), "/api/v1/%s/%s/%s", collection, id, verb);
    if (akn_daemon_write(link, "POST", path, body, err, sizeof(err), NULL, NULL) == 0) done(out, 1, reload, "%s", ok_message);
    else failed(out, what_failed, err);
}

void akn_companion_action(akn_daemon_link *link, const akn_companion *m, const char *node_id, const char *value, akn_action_result *out) {
    memset(out, 0, sizeof(*out));
    char err[200] = "", text[AKN_UI_REPLY_MAX + 8];
    const char *id;
    if (!akn_companion_action_verb(node_id, NULL)) {
        done(out, 0, 0, "Unknown control.");
        return; /* not handled: onionHEN's own controls, or a malformed ID */
    }
    out->handled = 1;
    out->notify = 1;

    if (strcmp(node_id, "refresh") == 0) {
        out->notify = 0;
        done(out, 1, AKN_LOAD_ALL, "Refreshed.");
        return;
    }
    if (!m->writes) {
        /* the pages offer no changes then; a stale page can still send one */
        done(out, 0, AKN_LOAD_ALL, "This AKENO daemon is read-only (native interface 1).");
        return;
    }
    if (strcmp(node_id, "presence") == 0) {
        if (akn_companion_set_presence(link, value ? value : "", err, sizeof(err)) == 0)
            done(out, 1, AKN_LOAD_PROFILE, "Your status is now %s.", akn_companion_presence_label(value));
        else
            failed(out, "Status not changed", err);
        return;
    }
    if ((id = after_prefix(node_id, "reply."))) {
        const akn_ui_conv *v = akn_companion_conv(m, id);
        if (typed_message(value, text, sizeof(text), out) != 0) return;
        if (send_message(link, id, text, err, sizeof(err)) == 0) done(out, 1, AKN_LOAD_MESSAGES, "Sent to %s.", v ? v->title : "the conversation");
        else failed(out, "Not sent", err);
        return;
    }
    if ((id = after_prefix(node_id, "dm."))) {
        const akn_ui_friend *f = akn_companion_friend(m, id);
        if (typed_message(value, text, sizeof(text), out) != 0) return;
        char body[96];
        snprintf(body, sizeof(body), "{\"userId\":\"%s\"}", id);
        akeno_json_doc *doc = NULL;
        const akeno_json *data = NULL;
        akn_conversation conv;
        memset(&conv, 0, sizeof(conv));
        /* finds the direct conversation, or opens it */
        if (akn_daemon_write(link, "POST", "/api/v1/conversations", body, err, sizeof(err), &doc, &data) != 0 ||
            akn_parse_direct_conversation(data, &conv) != 0 || !akn_companion_id_ok(conv.id)) {
            akeno_json_free(doc);
            failed(out, "Not sent", err[0] ? err : "the conversation could not be opened.");
            return;
        }
        akeno_json_free(doc);
        if (send_message(link, conv.id, text, err, sizeof(err)) == 0) done(out, 1, AKN_LOAD_MESSAGES, "Sent to %s.", f ? f->name : "your friend");
        else failed(out, "Not sent", err);
        return;
    }
    if ((id = after_prefix(node_id, "read."))) {
        const akn_ui_conv *v = akn_companion_conv(m, id);
        if (!v || v->last_message_id <= 0) {
            done(out, 0, AKN_LOAD_MESSAGES, "Nothing to mark as read.");
            return;
        }
        char body[64];
        snprintf(body, sizeof(body), "{\"lastReadMessageId\":%lld}", v->last_message_id);
        char ok[160];
        snprintf(ok, sizeof(ok), "Marked %s as read.", v->title);
        post_simple(link, out, "conversations", id, "read", body, AKN_LOAD_MESSAGES, ok, "Not marked as read");
        return;
    }
    if ((id = after_prefix(node_id, "pinv.")) || (id = after_prefix(node_id, "fpinv."))) {
        const akn_ui_friend *f = akn_companion_friend(m, id);
        if (!m->in_party || !m->party_id[0]) {
            done(out, 0, AKN_LOAD_PARTY, "You are not in a party any more.");
            return;
        }
        char body[96], path[160];
        snprintf(body, sizeof(body), "{\"userId\":\"%s\"}", id);
        snprintf(path, sizeof(path), "/api/v1/parties/%s/invitations", m->party_id);
        if (akn_daemon_write(link, "POST", path, body, err, sizeof(err), NULL, NULL) == 0) done(out, 1, AKN_LOAD_PARTY, "Invited %s to your party.", f ? f->name : "your friend");
        else failed(out, "Not invited", err);
        return;
    }
    if ((id = after_prefix(node_id, "pjoin.")) || (id = after_prefix(node_id, "fpjoin."))) {
        const char *party_id = id;
        if (strncmp(node_id, "fpjoin.", 7) == 0) {
            const akn_ui_friend *f = akn_companion_friend(m, id);
            party_id = f && f->party_id[0] ? f->party_id : NULL;
        }
        if (!party_id) {
            done(out, 0, AKN_LOAD_PARTY, "That party is not open to you any more.");
            return;
        }
        post_simple(link, out, "parties", party_id, "join", NULL, AKN_LOAD_PARTY | AKN_LOAD_MESSAGES, "You joined the party.", "Not joined");
        return;
    }
    if ((id = after_prefix(node_id, "pleave."))) {
        post_simple(link, out, "parties", id, "leave", NULL, AKN_LOAD_PARTY | AKN_LOAD_MESSAGES, "You left the party.", "Not left");
        return;
    }
    if (strcmp(node_id, "pcreate") == 0) {
        char name[96], body[512];
        trimmed(value ? value : "", name, sizeof(name));
        if (akn_party_create_json(name, 0, body, sizeof(body)) != 0) {
            done(out, 0, 0, "This party name cannot be used.");
            return;
        }
        if (akn_daemon_write(link, "POST", "/api/v1/parties", body, err, sizeof(err), NULL, NULL) == 0)
            done(out, 1, AKN_LOAD_PARTY | AKN_LOAD_MESSAGES, "Party created. Your friends can join it.");
        else
            failed(out, "No party created", err);
        return;
    }
    if ((id = after_prefix(node_id, "iacc."))) {
        post_simple(link, out, "invitations", id, "accept", NULL, AKN_LOAD_INBOX | AKN_LOAD_PARTY | AKN_LOAD_MESSAGES, "Invitation accepted.",
                    "Not accepted");
        return;
    }
    if ((id = after_prefix(node_id, "idec."))) {
        post_simple(link, out, "invitations", id, "decline", NULL, AKN_LOAD_INBOX, "Invitation declined.", "Not declined");
        return;
    }
    if ((id = after_prefix(node_id, "racc."))) {
        post_simple(link, out, "friends/requests", id, "accept", NULL, AKN_LOAD_INBOX | AKN_LOAD_FRIENDS, "Friend request accepted.",
                    "Not accepted");
        return;
    }
    if ((id = after_prefix(node_id, "rdec."))) {
        post_simple(link, out, "friends/requests", id, "decline", NULL, AKN_LOAD_INBOX, "Friend request declined.", "Not declined");
        return;
    }
    if (strcmp(node_id, "nread") == 0) {
        if (akn_daemon_write(link, "POST", "/api/v1/notifications/read", "{\"all\":true}", err, sizeof(err), NULL, NULL) == 0)
            done(out, 1, AKN_LOAD_INBOX, "Notifications marked as read.");
        else
            failed(out, "Not marked as read", err);
        return;
    }
    done(out, 0, 0, "Unknown control."); /* a route without a branch: the tests catch this */
}
