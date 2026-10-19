#include "fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "account.h"
#include "akeno/json.h"
#include "ipc.h"
#include "voice_link.h"

#define TIMEOUT_MS 8000
#define LOCAL_TIMEOUT_MS 3000
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
        } else if (!doc && err) {
            snprintf(err, err_len, "The AKENO daemon sent an answer that is not JSON.");
        }
    } else if (err) {
        if (reply.transport_error) snprintf(err, err_len, "The AKENO daemon did not answer.");
        else if (reply.status == 403) snprintf(err, err_len, "The AKENO daemon refused this (HTTP 403).");
        else snprintf(err, err_len, "The AKENO daemon answered HTTP %d.", reply.status);
    }
    akn_http_reply_free(&reply);
    return doc;
}

static akn_companion_state open_session(session *s, akn_daemon_info *info) {
    akn_http_reply reply;
    /* the liveness answer needs no key and says whether the native interface exists */
    akn_loopback_get(s->link->port, "/api/instance", NULL, LOCAL_TIMEOUT_MS, 4096, &reply);
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

akn_store *akn_store_new(void) { return calloc(1, sizeof(akn_store)); }

void akn_store_free(akn_store *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
    free(store);
}

static void forget(akn_store *st) {
    st->have_me = st->have_friends = st->have_convs = st->have_party = st->have_joinable = st->have_inbox = 0;
    st->thread_count = 0;
    st->presence[0] = '\0';
    st->events_ready = 0;
}

static int load_profile(session *s, akn_store *st, char *err, size_t err_len) {
    if (!st->daemon.online_id[0]) {
        snprintf(err, err_len, "The daemon did not say which account it is paired with.");
        return -1;
    }
    char path[160], seg[100];
    encode_segment(st->daemon.online_id, seg, sizeof(seg));
    snprintf(path, sizeof(path), "/api/v1/users/%s", seg);
    const akeno_json *data;
    akeno_json_doc *doc = proxy(s, "GET", path, NULL, &data, err, err_len);
    int ok = doc && akn_fill_profile(data, &st->me) == 0;
    akeno_json_free(doc);
    if (!ok) return -1;
    st->have_me = 1;
    akn_presence p = st->me.presence.status;
    snprintf(st->presence, sizeof(st->presence), "%s",
             p == AKN_PRES_AWAY ? "away" : p == AKN_PRES_DND ? "dnd" : p == AKN_PRES_OFFLINE ? "invisible" : "online");
    return 0;
}

static int load_friends(session *s, akn_store *st, char *err, size_t err_len) {
    const akeno_json *data;
    akeno_json_doc *doc = proxy(s, "GET", "/api/v1/friends", NULL, &data, err, err_len);
    int ok = doc && akn_fill_friends(data, &st->friends) == 0;
    akeno_json_free(doc);
    st->have_friends = ok;
    return ok ? 0 : -1;
}

static int load_party(session *s, akn_store *st, char *err, size_t err_len) {
    const akeno_json *data;
    akeno_json_doc *doc = proxy(s, "GET", "/api/v1/parties/current", NULL, &data, err, err_len);
    st->have_party = doc && akn_fill_current_party(data, &st->party) == 0;
    akeno_json_free(doc);
    if (!st->have_party) return -1;
    doc = proxy(s, "GET", "/api/v1/parties", NULL, &data, err, err_len);
    st->have_joinable = doc && akn_fill_joinable_parties(data, st->party.present ? st->party.id : "", &st->joinable) == 0;
    akeno_json_free(doc);
    return st->have_joinable ? 0 : -1;
}

static int load_inbox(session *s, akn_store *st, char *err, size_t err_len) {
    const akeno_json *data;
    akn_inbox *in = &st->inbox;
    memset(in, 0, sizeof(*in));
    akeno_json_doc *doc = proxy(s, "GET", "/api/v1/friends/requests", NULL, &data, err, err_len);
    int ok = doc && akn_fill_friend_requests(data, in) == 0;
    akeno_json_free(doc);
    doc = ok ? proxy(s, "GET", "/api/v1/invitations", NULL, &data, err, err_len) : NULL;
    ok = ok && doc && akn_fill_invitations(data, in) == 0;
    akeno_json_free(doc);
    doc = ok ? proxy(s, "GET", "/api/v1/notifications?limit=3", NULL, &data, err, err_len) : NULL;
    ok = ok && doc && akn_fill_notifications(data, in) == 0;
    akeno_json_free(doc);
    st->have_inbox = ok;
    return ok ? 0 : -1;
}

/* The latest messages of one conversation into the cache entry (one line each, oldest first). */
static void load_thread(session *s, akn_store *st, const akn_conversation *v, akn_thread *scratch, akn_thread_cache *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->conv_id, sizeof(out->conv_id), "%s", v->id);
    out->last_message_id = v->last_message_id;
    char path[160];
    snprintf(path, sizeof(path), "/api/v1/conversations/%s/messages?limit=%d", v->id, AKN_UI_THREAD);
    const akeno_json *data;
    akeno_json_doc *doc = proxy(s, "GET", path, NULL, &data, out->error, sizeof(out->error));
    memset(scratch, 0, sizeof(*scratch));
    if (!doc || akn_fill_thread(data, st->have_me ? st->me.id : "", scratch) != 0) {
        if (!out->error[0]) snprintf(out->error, sizeof(out->error), "The messages could not be read.");
        out->state = -1;
        akeno_json_free(doc);
        return;
    }
    akeno_json_free(doc);
    int first = scratch->count > AKN_UI_THREAD ? scratch->count - AKN_UI_THREAD : 0;
    for (int i = first; i < scratch->count; i++) {
        const akn_message *m = &scratch->items[i];
        akn_ui_message *u = &out->items[out->count++];
        snprintf(u->sender, sizeof(u->sender), "%s", m->mine ? "You" : m->sender_name[0] ? m->sender_name : "Someone");
        akn_chat_preview(m->body, u->body, sizeof(u->body));
        u->mine = m->mine;
        u->at = m->created_at;
    }
    out->state = 1;
    st->thread_loads++;
}

static int load_messages(session *s, akn_store *st, char *err, size_t err_len) {
    const akeno_json *data;
    akeno_json_doc *doc = proxy(s, "GET", "/api/v1/conversations", NULL, &data, err, err_len);
    int ok = doc && akn_fill_direct_conversations(data, &st->convs) == 0;
    akeno_json_free(doc);
    if (!ok) {
        st->have_convs = 0;
        return -1;
    }
    doc = proxy(s, "GET", "/api/v1/groups", NULL, &data, NULL, 0);
    if (doc) akn_add_group_conversations(data, &st->convs);
    akeno_json_free(doc);
    if (st->have_party && st->party.present && st->party.conversation_id[0])
        akn_add_party_conversation(&st->convs, st->party.conversation_id, st->party.id, st->party.name, st->party.member_count, 0);
    akn_conversations_sort(&st->convs);
    st->have_convs = 1;

    /* the latest messages of the conversations on the page; unchanged ones are kept */
    akn_thread_cache *next = calloc(AKN_UI_CONVS, sizeof(*next));
    akn_thread *scratch = calloc(1, sizeof(*scratch));
    if (!next || !scratch) {
        free(next);
        free(scratch);
        snprintf(err, err_len, "Out of memory while reading messages.");
        return -1;
    }
    int n = 0;
    for (int i = 0; i < st->convs.count && n < AKN_UI_CONVS; i++) {
        const akn_conversation *v = &st->convs.items[i];
        if (!akn_companion_id_ok(v->id)) continue;
        const akn_thread_cache *old = NULL;
        for (int k = 0; k < st->thread_count; k++)
            if (strcmp(st->threads[k].conv_id, v->id) == 0) old = &st->threads[k];
        if (old && old->state == 1 && old->last_message_id == v->last_message_id) next[n] = *old;
        else load_thread(s, st, v, scratch, &next[n]);
        n++;
    }
    memcpy(st->threads, next, sizeof(st->threads));
    st->thread_count = n;
    free(next);
    free(scratch);
    return 0;
}

int akn_store_voice(akn_daemon_link *link, akn_store *st) {
    akn_ui_voice next;
    akn_voice_load(link, &next);
    int change = !st->have_voice || akn_voice_signature(&next, 0) != akn_voice_signature(&st->voice, 0) ? 2
                 : akn_voice_signature(&next, 1) != akn_voice_signature(&st->voice, 1)                     ? 1
                                                                                                           : 0;
    st->voice = next;
    st->have_voice = 1;
    return change;
}

unsigned akn_store_load(akn_daemon_link *link, akn_store *st, unsigned sections, long long now) {
    if (sections & AKN_LOAD_VOICE) akn_store_voice(link, st); /* independent of the daemon's state */
    session s;
    memset(&s, 0, sizeof(s));
    s.link = link;
    akn_daemon_info info;
    memset(&info, 0, sizeof(info));
    akn_companion_state state = open_session(&s, &info);
    unsigned failed = 0;
    char err[200] = "";
    if (state != AKN_COMP_READY) {
        /* nothing shown from before: the page says why it is empty */
        forget(st);
        st->daemon = info;
        st->state = state;
        snprintf(st->diag.last_error, sizeof(st->diag.last_error), "%s", state == AKN_COMP_NOT_PAIRED ? "Not paired" : "No daemon session");
        failed = sections & ~(unsigned)AKN_LOAD_VOICE;
    } else {
        if (st->state != AKN_COMP_READY || strcmp(st->daemon.online_id, info.online_id) != 0) {
            forget(st); /* a different account or a new daemon session: read everything */
            sections = AKN_LOAD_ALL;
        }
        st->state = state;
        st->daemon = info;
        if (!st->events_ready) {
            st->events_after = info.events_next;
            st->events_ready = 1;
        }
        /* order matters: your ID decides which messages are yours; the party adds its chat */
        if ((sections & AKN_LOAD_PROFILE) && load_profile(&s, st, err, sizeof(err)) != 0) failed |= AKN_LOAD_PROFILE;
        if ((sections & AKN_LOAD_FRIENDS) && load_friends(&s, st, err, sizeof(err)) != 0) failed |= AKN_LOAD_FRIENDS;
        if ((sections & AKN_LOAD_PARTY) && load_party(&s, st, err, sizeof(err)) != 0) failed |= AKN_LOAD_PARTY;
        if ((sections & AKN_LOAD_MESSAGES) && load_messages(&s, st, err, sizeof(err)) != 0) failed |= AKN_LOAD_MESSAGES;
        if ((sections & AKN_LOAD_INBOX) && load_inbox(&s, st, err, sizeof(err)) != 0) failed |= AKN_LOAD_INBOX;
        if (failed) snprintf(st->diag.last_error, sizeof(st->diag.last_error), "%s", err[0] ? err : "A part could not be loaded.");
        else st->diag.loaded_at = now;
    }
    st->diag.sections_failed = (int)failed;
    st->diag.requests = link->requests;
    st->diag.failures = link->failures;
    memset(s.key, 0, sizeof(s.key));
    return failed;
}

unsigned akn_store_poll(akn_daemon_link *link, akn_store *st, int *changed) {
    *changed = 0;
    if (st->state != AKN_COMP_READY || !st->events_ready) return AKN_LOAD_ALL;
    session s;
    memset(&s, 0, sizeof(s));
    s.link = link;
    char path[320];
    snprintf(path, sizeof(path), "%s/native.key", link->akeno_dir);
    if (akn_read_native_key(path, s.key, sizeof(s.key)) != 0) return AKN_LOAD_ALL;
    snprintf(path, sizeof(path), "/api/events?after=%llu", st->events_after);
    akn_http_reply reply;
    s.link->requests++;
    akn_loopback_get(link->port, path, s.key, LOCAL_TIMEOUT_MS, REPLY_MAX, &reply);
    memset(s.key, 0, sizeof(s.key));
    if (reply.transport_error || reply.status != 200 || !reply.body) {
        link->failures++;
        akn_http_reply_free(&reply);
        return AKN_LOAD_ALL; /* the daemon went away or restarted: the session check says which */
    }
    akn_events_result r;
    memset(&r, 0, sizeof(r));
    r.next = st->events_after;
    int rc = akn_apply_events(reply.body, reply.len, st->have_friends ? &st->friends : NULL, NULL, &r);
    akn_http_reply_free(&reply);
    if (rc != 0) return AKN_LOAD_ALL;
    if (r.next < st->events_after) {
        st->events_ready = 0; /* a new daemon numbers from 1 again: the next load starts from its hello */
        return AKN_LOAD_ALL;
    }
    st->events_after = r.next;
    if (r.presence_applied && st->have_friends) {
        akn_friends_sort(&st->friends);
        akn_friends_count(&st->friends);
        *changed = 1;
    }
    unsigned want = 0;
    if (r.flags & (AKN_EVF_GAP | AKN_EVF_REVOKED)) return AKN_LOAD_ALL;
    if (r.flags & AKN_EVF_FRIENDS) want |= AKN_LOAD_FRIENDS | AKN_LOAD_INBOX;
    if (r.flags & AKN_EVF_PROFILE) want |= AKN_LOAD_PROFILE;
    if (r.flags & (AKN_EVF_MESSAGES | AKN_EVF_GROUPS)) want |= AKN_LOAD_MESSAGES;
    if (r.flags & AKN_EVF_PARTY) want |= AKN_LOAD_PARTY | AKN_LOAD_MESSAGES;
    if (r.flags & AKN_EVF_INVITATIONS) want |= AKN_LOAD_INBOX;
    if (r.flags & (AKN_EVF_STATE | AKN_EVF_ACTIVITY)) *changed = 1; /* the daemon's state is read with every load */
    return want;
}

void akn_store_model(const akn_store *st, akn_companion *out, long long now) {
    akn_companion_extra x;
    memset(&x, 0, sizeof(x));
    x.me = st->have_me ? &st->me : NULL;
    x.joinable = st->have_joinable ? &st->joinable : NULL;
    x.threads = st->threads;
    x.thread_count = st->thread_count;
    x.diag = &st->diag;
    x.voice = st->have_voice ? &st->voice : NULL;
    akn_companion_build_ex(out, st->state, st->state == AKN_COMP_READY || st->daemon.ipc ? &st->daemon : NULL, st->have_friends ? &st->friends : NULL,
                           st->have_convs ? &st->convs : NULL, st->have_party ? &st->party : NULL, st->have_inbox ? &st->inbox : NULL, st->presence,
                           now, &x);
}

int akn_daemon_write(akn_daemon_link *link, const char *method, const char *path, const char *body, char *err, size_t err_len,
                     akeno_json_doc **out_doc, const akeno_json **out_data) {
    if (out_doc) *out_doc = NULL;
    if (out_data) *out_data = NULL;
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
        if (doc && out_doc) {
            *out_doc = doc;
            if (out_data) *out_data = data;
        } else {
            akeno_json_free(doc);
        }
    }
    memset(s.key, 0, sizeof(s.key));
    return rc;
}

void akn_companion_load(akn_daemon_link *link, akn_companion *out) {
    akn_store *st = akn_store_new();
    if (!st) {
        akn_companion_build(out, AKN_COMP_NO_DAEMON, NULL, NULL, NULL, NULL, NULL, "", 0);
        return;
    }
    long long now = (long long)time(NULL);
    akn_store_load(link, st, AKN_LOAD_ALL, now);
    akn_store_model(st, out, now);
    akn_store_free(st);
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
    return akn_daemon_write(link, "PUT", "/api/v1/presence", body, err, err_len, NULL, NULL);
}

int akn_companion_accept_invitation(akn_daemon_link *link, const char *id, char *err, size_t err_len) {
    if (!akn_companion_id_ok(id)) {
        snprintf(err, err_len, "No invitation.");
        return -1;
    }
    char path[160];
    snprintf(path, sizeof(path), "/api/v1/invitations/%s/accept", id);
    return akn_daemon_write(link, "POST", path, NULL, err, err_len, NULL, NULL);
}
