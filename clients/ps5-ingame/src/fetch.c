#include "fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "account.h"
#include "akeno/json.h"
#include "ipc.h"

#define TIMEOUT_MS 8000
#define REPLY_MAX (1024 * 1024)

typedef struct {
    akn_daemon_link *link;
    char key[AKN_NATIVE_KEY_LEN + 1];
} session;

static int call(session *s, const char *method, const char *path, const char *body, akn_http_reply *reply) {
    s->link->requests++;
    akn_loopback_request(s->link->port, method, path, s->key[0] ? s->key : NULL, body, TIMEOUT_MS, REPLY_MAX, reply);
    if (reply->transport_error || reply->status != 200) {
        s->link->failures++;
        return -1;
    }
    return 0;
}

/* A proxied server answer: the document (caller frees) with *data, or NULL. */
static akeno_json_doc *proxy(session *s, const char *method, const char *path, const char *body, const akeno_json **data, char *err, size_t err_len) {
    akn_http_reply reply;
    *data = NULL;
    akeno_json_doc *doc = NULL;
    if (call(s, method, path, body, &reply) == 0 && reply.body) {
        akn_proxy_result res;
        memset(&res, 0, sizeof(res));
        doc = akn_proxy_parse(reply.body, reply.len, &res, data);
        if (doc && !res.ok) {
            if (err) snprintf(err, err_len, "%s", res.message[0] ? res.message : res.code[0] ? res.code : "refused");
            akeno_json_free(doc);
            doc = NULL;
            *data = NULL;
        }
    } else if (err) {
        snprintf(err, err_len, "The AKENO daemon did not answer (HTTP %d).", reply.status);
    }
    akn_http_reply_free(&reply);
    return doc;
}

static akn_companion_state open_session(session *s, akn_daemon_info *info) {
    akn_http_reply reply;
    /* the liveness answer needs no key and says whether the native interface exists */
    akn_loopback_get(s->link->port, "/api/instance", NULL, 3000, 4096, &reply);
    s->link->requests++;
    int ipc = 0, akeno = 0;
    if (!reply.transport_error && reply.status == 200 && reply.body) {
        akeno_json_doc *doc = akeno_json_parse(reply.body, reply.len, NULL, 0);
        const akeno_json *root = akeno_json_root(doc);
        akeno = strcmp(akeno_json_get_string(root, "app", ""), "akeno-network-console") == 0;
        int64_t v = 0;
        if (akeno_json_int64(akeno_json_get(root, "nativeIpc"), &v) == 0) ipc = (int)v;
        akeno_json_free(doc);
    }
    akn_http_reply_free(&reply);
    if (!akeno) return AKN_COMP_NO_DAEMON;
    if (ipc <= 0) return AKN_COMP_NO_INTERFACE;
    char path[320];
    snprintf(path, sizeof(path), "%s/native.key", s->link->akeno_dir);
    if (akn_read_native_key(path, s->key, sizeof(s->key)) != 0) return AKN_COMP_NO_KEY;
    if (call(s, "GET", "/api/native/hello", NULL, &reply) != 0 || akn_parse_daemon_hello(reply.body, reply.len, info) != 0) {
        int refused = reply.status == 403; /* a key from before a daemon restart */
        akn_http_reply_free(&reply);
        return refused ? AKN_COMP_NO_KEY : AKN_COMP_NO_DAEMON;
    }
    akn_http_reply_free(&reply);
    if (call(s, "GET", "/api/state", NULL, &reply) == 0) akn_parse_daemon_state(reply.body, reply.len, info);
    akn_http_reply_free(&reply);
    return info->paired ? AKN_COMP_READY : AKN_COMP_NOT_PAIRED;
}

static void encode_segment(const char *in, char *out, size_t len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < len; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
        else out[o++] = '%', out[o++] = hex[c >> 4], out[o++] = hex[c & 15];
    }
    out[o] = '\0';
}

void akn_companion_load(akn_daemon_link *link, akn_companion *out) {
    session s;
    memset(&s, 0, sizeof(s));
    s.link = link;
    akn_daemon_info *info = calloc(1, sizeof(*info));
    akn_friend_list *friends = calloc(1, sizeof(*friends));
    akn_conv_list *convs = calloc(1, sizeof(*convs));
    akn_party *party = calloc(1, sizeof(*party));
    akn_inbox *inbox = calloc(1, sizeof(*inbox));
    akn_profile *me = calloc(1, sizeof(*me));
    if (!info || !friends || !convs || !party || !inbox || !me) {
        akn_companion_build(out, AKN_COMP_NO_DAEMON, NULL, NULL, NULL, NULL, NULL, "", 0);
        goto done;
    }
    akn_companion_state state = open_session(&s, info);
    int have_friends = 0, have_convs = 0, have_party = 0, have_inbox = 0;
    char presence[16] = "";
    if (state == AKN_COMP_READY) {
        const akeno_json *data;
        akeno_json_doc *doc = proxy(&s, "GET", "/api/v1/friends", NULL, &data, NULL, 0);
        have_friends = doc && akn_fill_friends(data, friends) == 0;
        akeno_json_free(doc);
        doc = proxy(&s, "GET", "/api/v1/conversations", NULL, &data, NULL, 0);
        have_convs = doc && akn_fill_direct_conversations(data, convs) == 0;
        akeno_json_free(doc);
        doc = have_convs ? proxy(&s, "GET", "/api/v1/groups", NULL, &data, NULL, 0) : NULL;
        if (doc) akn_add_group_conversations(data, convs);
        akeno_json_free(doc);
        doc = proxy(&s, "GET", "/api/v1/parties/current", NULL, &data, NULL, 0);
        have_party = doc && akn_fill_current_party(data, party) == 0;
        akeno_json_free(doc);
        if (have_convs && have_party && party->present && party->conversation_id[0])
            akn_add_party_conversation(convs, party->conversation_id, party->id, party->name, party->member_count, 0);
        doc = proxy(&s, "GET", "/api/v1/friends/requests", NULL, &data, NULL, 0);
        have_inbox = doc && akn_fill_friend_requests(data, inbox) == 0;
        akeno_json_free(doc);
        doc = have_inbox ? proxy(&s, "GET", "/api/v1/invitations", NULL, &data, NULL, 0) : NULL;
        if (doc) akn_fill_invitations(data, inbox);
        akeno_json_free(doc);
        doc = have_inbox ? proxy(&s, "GET", "/api/v1/notifications?limit=1", NULL, &data, NULL, 0) : NULL;
        if (doc) akn_fill_notifications(data, inbox);
        akeno_json_free(doc);
        if (info->online_id[0]) {
            char path[160], seg[100];
            encode_segment(info->online_id, seg, sizeof(seg));
            snprintf(path, sizeof(path), "/api/v1/users/%s", seg);
            doc = proxy(&s, "GET", path, NULL, &data, NULL, 0);
            if (doc && akn_fill_profile(data, me) == 0) {
                akn_presence p = me->presence.status;
                snprintf(presence, sizeof(presence), "%s", p == AKN_PRES_AWAY ? "away" : p == AKN_PRES_DND ? "dnd" : p == AKN_PRES_OFFLINE ? "invisible" : "online");
            }
            akeno_json_free(doc);
        }
    }
    akn_companion_build(out, state, info, have_friends ? friends : NULL, have_convs ? convs : NULL, have_party ? party : NULL,
                        have_inbox ? inbox : NULL, presence, (long long)time(NULL));
done:
    memset(s.key, 0, sizeof(s.key));
    free(info);
    free(friends);
    free(convs);
    free(party);
    free(inbox);
    free(me);
}

static int write_once(akn_daemon_link *link, const char *method, const char *path, const char *body, char *err, size_t err_len) {
    session s;
    memset(&s, 0, sizeof(s));
    s.link = link;
    akn_daemon_info info;
    memset(&info, 0, sizeof(info));
    int rc = -1;
    if (open_session(&s, &info) != AKN_COMP_READY) {
        snprintf(err, err_len, "The AKENO daemon is not ready.");
    } else if (info.ipc < 2) {
        snprintf(err, err_len, "This AKENO daemon cannot take changes (native interface 1).");
    } else {
        const akeno_json *data;
        akeno_json_doc *doc = proxy(&s, method, path, body, &data, err, err_len);
        rc = doc ? 0 : -1;
        akeno_json_free(doc);
    }
    memset(s.key, 0, sizeof(s.key));
    return rc;
}

int akn_companion_set_presence(akn_daemon_link *link, const char *status, char *err, size_t err_len) {
    static const char *const ok[] = { "online", "away", "dnd", "invisible" };
    int valid = 0;
    for (size_t i = 0; i < 4; i++) valid |= strcmp(status, ok[i]) == 0;
    if (!valid) {
        snprintf(err, err_len, "Unknown status.");
        return -1;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"status\":\"%s\"}", status);
    return write_once(link, "PUT", "/api/v1/presence", body, err, err_len);
}

int akn_companion_accept_invitation(akn_daemon_link *link, const char *id, char *err, size_t err_len) {
    size_t n = strlen(id);
    for (size_t i = 0; i < n; i++)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) n = 0;
    if (n == 0 || n > 64) {
        snprintf(err, err_len, "No invitation.");
        return -1;
    }
    char path[160];
    snprintf(path, sizeof(path), "/api/v1/invitations/%s/accept", id);
    return write_once(link, "POST", path, NULL, err, err_len);
}
