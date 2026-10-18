/*
 * The social worker: reads friends, profiles, game activity, conversations, the party, invitations
 * and notifications from the AKENO daemon's native interface on 127.0.0.1
 * (docs/PS5_NATIVE_PHASE2.md), follows the daemon's event ring, and makes the writes the daemon's
 * native write list allows (IPC 2), one at a time, in the order they were asked for. The daemon
 * keeps the only realtime session, the system notifications and the game detection.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akeno/json.h"
#include "app.h"
#include "crypto_util.h"
#include "ipc.h"
#include "net.h"

#define RECONNECT_MS 3000
#define EVENTS_MS 1000
#define STATE_MS 5000
/* The daemon answers one request at a time, so a local answer can wait behind a proxied one. */
#define LOCAL_TIMEOUT_MS 5000    /* daemon-local answers (hello, state, events) */
#define PROXY_TIMEOUT_MS 15000   /* through the daemon to the server */
#define REPLY_MAX (1024 * 1024)
#define FOCUS_MS 8000            /* repeat the open conversation to the daemon (it forgets it after 20 s) */
#define THREAD_PAGE 50
#define OLDER_PAGE 40

const char *akn_ipc_state_text(akn_ipc_state state) {
    switch (state) {
    case AKN_IPC_UNCHECKED: return "Connecting to the AKENO daemon...";
    case AKN_IPC_NO_DAEMON: return "The AKENO daemon is not running";
    case AKN_IPC_NOT_SUPPORTED: return "This AKENO daemon has no native interface";
    case AKN_IPC_NO_KEY: return "The daemon's native key is missing";
    case AKN_IPC_UNSAFE_KEY: return "The daemon's native key file is not private: not used";
    case AKN_IPC_KEY_REJECTED: return "The daemon refused the native key";
    case AKN_IPC_READY: return "Connected to the AKENO daemon";
    }
    return "?";
}

int akn_social_playing_count(const akn_social *s) { return s->friends ? s->friends->playing : 0; }

int akn_social_playing_index(const akn_social *s, int row) {
    if (!s->friends || row < 0) return -1;
    for (int i = 0, n = 0; i < s->friends->count; i++)
        if (s->friends->items[i].presence.game.present && n++ == row) return i;
    return -1;
}

/* ---- Requests ---------------------------------------------------------------------------------- */

typedef struct {
    akn_app *app;
    char key[AKN_NATIVE_KEY_LEN + 1];
} worker_ctx;

/* Percent-encodes a user ID or Online ID for a path segment. */
static void encode_segment(const char *in, char *out, size_t len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < len; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static void set_ipc(akn_app *app, akn_ipc_state state, const char *detail) {
    SDL_LockMutex(app->lock);
    int changed = app->social.ipc != state;
    app->social.ipc = state;
    snprintf(app->social.ipc_detail, sizeof(app->social.ipc_detail), "%s", detail ? detail : "");
    SDL_UnlockMutex(app->lock);
    if (changed)
        akn_logf(&app->log, state == AKN_IPC_READY ? AKN_LOG_INFO : AKN_LOG_WARN, "ipc.state", "%s%s%s", akn_ipc_state_text(state),
                 detail && detail[0] ? ": " : "", detail ? detail : "");
}

/*
 * One GET with the native key. Returns the reply (status 200 with a body) or marks the interface
 * down: a transport error means the daemon went away, 403 NATIVE_KEY_INVALID that it restarted.
 * Proxy answers are always 200 or a daemon error; the server's status is inside.
 */
static int call(worker_ctx *w, const char *method, const char *path, const char *body, int timeout_ms, akn_http_reply *reply) {
    akn_app *app = w->app;
    SDL_LockMutex(app->lock);
    app->social.requests++;
    SDL_UnlockMutex(app->lock);
    akn_loopback_request(app->platform.daemon_port, method, path, w->key, body, timeout_ms, REPLY_MAX, reply);
    if (reply->transport_error) {
        set_ipc(app, AKN_IPC_NO_DAEMON, "the daemon stopped answering");
        return -1;
    }
    if (reply->status == 403 && reply->body && strstr(reply->body, "NATIVE_KEY_INVALID")) {
        set_ipc(app, AKN_IPC_KEY_REJECTED, "the daemon has a new key (restarted?)");
        return -1;
    }
    return 0;
}

static int get(worker_ctx *w, const char *path, int timeout_ms, akn_http_reply *reply) { return call(w, "GET", path, NULL, timeout_ms, reply); }

/* Reads a proxied server answer. Returns the document (caller frees) with *data set, or NULL with
 * a readable error in err. */
static akeno_json_doc *proxy(worker_ctx *w, const char *method, const char *path, const char *body, const akeno_json **data, char *err,
                             size_t err_len, int *server_status) {
    akn_http_reply reply;
    *data = NULL;
    if (server_status) *server_status = 0;
    if (call(w, method, path, body, PROXY_TIMEOUT_MS, &reply) != 0) {
        snprintf(err, err_len, "The AKENO daemon is not answering.");
        akn_http_reply_free(&reply);
        return NULL;
    }
    akn_proxy_result res;
    memset(&res, 0, sizeof(res));
    akeno_json_doc *doc = reply.body ? akn_proxy_parse(reply.body, reply.len, &res, data) : NULL;
    akn_http_reply_free(&reply);
    if (server_status) *server_status = res.status;
    if (doc && res.ok) return doc;
    if (!doc) snprintf(err, err_len, "The daemon sent an answer AKENO Native cannot read.");
    else if (strcmp(res.code, "NOT_PAIRED") == 0 || strcmp(res.code, "UNPAIRED") == 0)
        snprintf(err, err_len, "This console is not paired. Pair it on the AKENO page first.");
    else if (strcmp(res.code, "NOT_ALLOWED_FOR_NATIVE") == 0) snprintf(err, err_len, "The AKENO daemon does not allow this from AKENO Native.");
    else if (res.status == 0) snprintf(err, err_len, "The daemon could not reach the AKENO server (%s).", res.code[0] ? res.code : "offline");
    else if (res.message[0]) snprintf(err, err_len, "%s", res.message); /* the server's own explanation */
    else snprintf(err, err_len, "The server answered %d.", res.status);
    akeno_json_free(doc);
    *data = NULL;
    SDL_LockMutex(w->app->lock);
    w->app->social.errors++;
    SDL_UnlockMutex(w->app->lock);
    return NULL;
}

static akeno_json_doc *get_proxy(worker_ctx *w, const char *path, const akeno_json **data, char *err, size_t err_len, int *server_status) {
    return proxy(w, "GET", path, NULL, data, err, err_len, server_status);
}

/* ---- Writes ------------------------------------------------------------------------------------- */

static int writes_allowed(const akn_social *s) { return s->ipc == AKN_IPC_READY && s->daemon.ipc >= 2; }

/* Queues a write (lock held). The same write waiting already is not queued twice (a button pressed
 * again while the first press is still on its way). */
static int enqueue_locked(akn_social *s, const akn_action *a) {
    if (!writes_allowed(s)) return -1;
    for (int i = 0; i < s->action_count; i++) {
        const akn_action *q = &s->actions[i];
        if (q->kind == a->kind && strcmp(q->path, a->path) == 0 && strcmp(q->body, a->body) == 0) return 0;
    }
    if (s->action_count >= AKN_ACTIONS_MAX) return -1;
    s->actions[s->action_count++] = *a;
    return 0;
}

static void set_result_locked(akn_social *s, int error, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void set_result_locked(akn_social *s, int error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->result, sizeof(s->result), fmt, ap);
    va_end(ap);
    s->result_error = error;
    s->result_seq++;
}

/* The open conversation is on screen and has newer messages than the last ones marked read: mark
 * them read on the server (lock held). */
static void mark_read_locked(akn_social *s) {
    if (!s->thread || !s->thread_conv.id[0] || strcmp(s->focus_conv, s->thread_conv.id) != 0) return;
    if (s->thread->newest_id <= s->read_sent_id || !writes_allowed(s)) return;
    akn_action a;
    memset(&a, 0, sizeof(a));
    a.kind = AKN_ACT_READ;
    snprintf(a.method, sizeof(a.method), "POST");
    snprintf(a.path, sizeof(a.path), "/api/v1/conversations/%s/read", s->thread_conv.id);
    snprintf(a.body, sizeof(a.body), "{\"lastReadMessageId\":%lld}", s->thread->newest_id);
    snprintf(a.conversation_id, sizeof(a.conversation_id), "%s", s->thread_conv.id);
    if (enqueue_locked(s, &a) != 0) return;
    s->read_sent_id = s->thread->newest_id;
    int i = akn_conversations_find(s->convs, s->thread_conv.id);
    if (i >= 0 && s->convs->items[i].unread) {
        s->convs->items[i].unread = 0;
        akn_conversations_sort(s->convs);
    }
}

static void connect_daemon(worker_ctx *w) {
    akn_app *app = w->app;
    akn_account_view probe;
    memset(&probe, 0, sizeof(probe));
    akn_net_probe_daemon(app->platform.daemon_port, 1500, &probe);
    if (probe.daemon != AKN_DAEMON_RUNNING) {
        set_ipc(app, AKN_IPC_NO_DAEMON, probe.daemon == AKN_DAEMON_OTHER ? "another program answers on its port" : "");
        return;
    }
    if (probe.daemon_ipc <= 0) {
        char detail[80];
        snprintf(detail, sizeof(detail), "version %s", probe.daemon_version);
        set_ipc(app, AKN_IPC_NOT_SUPPORTED, detail);
        return;
    }
    char path[320];
    snprintf(path, sizeof(path), "%s/native.key", app->platform.akeno_dir);
    int rc = akn_read_native_key(path, w->key, sizeof(w->key));
    if (rc != 0) {
        set_ipc(app, rc == -1 ? AKN_IPC_NO_KEY : AKN_IPC_UNSAFE_KEY, "");
        return;
    }
    akn_http_reply reply;
    if (get(w, "/api/native/hello", LOCAL_TIMEOUT_MS, &reply) != 0) {
        akn_http_reply_free(&reply);
        return;
    }
    akn_daemon_info info;
    memset(&info, 0, sizeof(info));
    if (reply.status != 200 || akn_parse_daemon_hello(reply.body, reply.len, &info) != 0) {
        char detail[80];
        snprintf(detail, sizeof(detail), "unexpected answer (HTTP %d)", reply.status);
        set_ipc(app, AKN_IPC_NO_DAEMON, detail);
        akn_http_reply_free(&reply);
        return;
    }
    akn_http_reply_free(&reply);
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    s->daemon = info;
    s->events_after = info.events_next;
    s->reconnects++;
    /* (Re)load what is shown; events before this moment are not needed. */
    s->want |= AKN_WANT_STATE | AKN_WANT_FRIENDS | AKN_WANT_ME | AKN_WANT_CONVERSATIONS | AKN_WANT_INBOX | AKN_WANT_PARTY;
    if (s->profile_handle[0]) s->want |= AKN_WANT_PROFILE;
    if (s->thread_conv.id[0]) s->want |= AKN_WANT_THREAD;
    s->focus_dirty = 1;
    if (s->own_activity_load != AKN_LOAD_NONE) s->want |= AKN_WANT_OWN_ACTIVITY;
    SDL_UnlockMutex(app->lock);
    char detail[120];
    snprintf(detail, sizeof(detail), "protocol %d, daemon %s (%s)", info.ipc, info.version, info.build);
    set_ipc(app, AKN_IPC_READY, detail);
}

static void load_state(worker_ctx *w) {
    akn_app *app = w->app;
    akn_http_reply reply;
    if (get(w, "/api/state", LOCAL_TIMEOUT_MS, &reply) == 0 && reply.status == 200) {
        SDL_LockMutex(app->lock);
        akn_daemon_info info = app->social.daemon;
        if (akn_parse_daemon_state(reply.body, reply.len, &info) == 0) app->social.daemon = info;
        SDL_UnlockMutex(app->lock);
    }
    akn_http_reply_free(&reply);
}

static void load_friends(worker_ctx *w) {
    akn_app *app = w->app;
    SDL_LockMutex(app->lock);
    if (app->social.friends_load != AKN_LOAD_OK) app->social.friends_load = AKN_LOAD_LOADING;
    SDL_UnlockMutex(app->lock);
    akn_friend_list *list = calloc(1, sizeof(*list));
    char err[200] = "";
    const akeno_json *data;
    akeno_json_doc *doc = list ? get_proxy(w, "/api/v1/friends", &data, err, sizeof(err), NULL) : NULL;
    int ok = doc && akn_fill_friends(data, list) == 0;
    akeno_json_free(doc);
    if (doc && !ok) snprintf(err, sizeof(err), "The friend list has an unexpected shape.");
    if (ok) {
        doc = get_proxy(w, "/api/v1/friends/requests", &data, err, sizeof(err), NULL);
        if (doc) akn_fill_requests(data, list); /* counts only; the list stands without them */
        akeno_json_free(doc);
    }
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (ok) {
        free(s->friends);
        s->friends = list;
        list = NULL;
        s->friends_load = AKN_LOAD_OK;
        s->friends_error[0] = '\0';
        s->friends_at = akn_platform_now_ms();
        s->friends_gen++;
    } else {
        if (s->friends_load != AKN_LOAD_OK) s->friends_load = AKN_LOAD_ERROR;
        snprintf(s->friends_error, sizeof(s->friends_error), "%s", err[0] ? err : "The friend list could not be loaded.");
    }
    int count = s->friends ? s->friends->count : 0, online = s->friends ? s->friends->online : 0;
    int incoming = s->friends ? s->friends->incoming_requests : 0;
    SDL_UnlockMutex(app->lock);
    free(list);
    /* Counts only: the log never names other people. */
    if (ok) akn_logf(&app->log, AKN_LOG_INFO, "social.friends", "%d friends, %d online, %d incoming requests", count, online, incoming);
    else akn_logf(&app->log, AKN_LOG_WARN, "social.friends", "%s", err);
}

static void load_activity(worker_ctx *w, const char *handle, akn_activity *out, int *ok) {
    char path[160], seg[100];
    encode_segment(handle, seg, sizeof(seg));
    snprintf(path, sizeof(path), "/api/v1/users/%s/activity", seg);
    char err[200];
    const akeno_json *data;
    akeno_json_doc *doc = get_proxy(w, path, &data, err, sizeof(err), NULL);
    *ok = doc && akn_fill_activity(data, out) == 0;
    akeno_json_free(doc);
}

static void load_profile(worker_ctx *w) {
    akn_app *app = w->app;
    char handle[48];
    SDL_LockMutex(app->lock);
    snprintf(handle, sizeof(handle), "%s", app->social.profile_handle);
    SDL_UnlockMutex(app->lock);
    if (!handle[0]) return;
    akn_profile *p = calloc(1, sizeof(*p));
    akn_activity *a = calloc(1, sizeof(*a));
    char path[160], seg[100], err[200] = "";
    encode_segment(handle, seg, sizeof(seg));
    snprintf(path, sizeof(path), "/api/v1/users/%s", seg);
    const akeno_json *data;
    akeno_json_doc *doc = p && a ? get_proxy(w, path, &data, err, sizeof(err), NULL) : NULL;
    int ok = doc && akn_fill_profile(data, p) == 0;
    akeno_json_free(doc);
    if (doc && !ok) snprintf(err, sizeof(err), "The profile has an unexpected shape.");
    int activity_ok = 0;
    if (ok) load_activity(w, p->id[0] ? p->id : handle, a, &activity_ok); /* hidden history is not an error */
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (strcmp(s->profile_handle, handle) == 0) { /* still the profile on screen */
        if (ok) {
            s->profile = *p;
            s->profile_load = AKN_LOAD_OK;
            s->profile_error[0] = '\0';
            if (activity_ok) s->profile_activity = *a;
            s->profile_activity_load = activity_ok ? AKN_LOAD_OK : AKN_LOAD_ERROR;
        } else {
            s->profile_load = AKN_LOAD_ERROR;
            snprintf(s->profile_error, sizeof(s->profile_error), "%s", err[0] ? err : "The profile could not be loaded.");
        }
    }
    SDL_UnlockMutex(app->lock);
    free(p);
    free(a);
    akn_logf(&app->log, ok ? AKN_LOG_INFO : AKN_LOG_WARN, "social.profile", "%s%s", ok ? "profile loaded" : "profile: ", ok ? "" : err);
}

static void load_own_activity(worker_ctx *w) {
    akn_app *app = w->app;
    char handle[48];
    SDL_LockMutex(app->lock);
    snprintf(handle, sizeof(handle), "%s", app->social.daemon.online_id);
    if (app->social.own_activity_load != AKN_LOAD_OK) app->social.own_activity_load = AKN_LOAD_LOADING;
    SDL_UnlockMutex(app->lock);
    akn_activity *a = calloc(1, sizeof(*a));
    int ok = 0;
    if (a && handle[0]) load_activity(w, handle, a, &ok);
    SDL_LockMutex(app->lock);
    if (ok) app->social.own_activity = *a;
    if (ok || app->social.own_activity_load != AKN_LOAD_OK) app->social.own_activity_load = ok ? AKN_LOAD_OK : AKN_LOAD_ERROR;
    SDL_UnlockMutex(app->lock);
    free(a);
}

/* Your own profile summary: picture and display name for Home. */
static void load_me(worker_ctx *w) {
    akn_app *app = w->app;
    char handle[48], path[160], seg[100], err[200];
    SDL_LockMutex(app->lock);
    snprintf(handle, sizeof(handle), "%s", app->social.daemon.online_id);
    SDL_UnlockMutex(app->lock);
    if (!handle[0]) return;
    encode_segment(handle, seg, sizeof(seg));
    snprintf(path, sizeof(path), "/api/v1/users/%s", seg);
    akn_profile *p = calloc(1, sizeof(*p));
    const akeno_json *data;
    akeno_json_doc *doc = p ? get_proxy(w, path, &data, err, sizeof(err), NULL) : NULL;
    int ok = doc && akn_fill_profile(data, p) == 0;
    akeno_json_free(doc);
    SDL_LockMutex(app->lock);
    if (ok) app->social.me = *p;
    app->social.me_load = ok ? AKN_LOAD_OK : AKN_LOAD_ERROR;
    SDL_UnlockMutex(app->lock);
    free(p);
}

static void poll_events(worker_ctx *w) {
    akn_app *app = w->app;
    char path[80];
    SDL_LockMutex(app->lock);
    snprintf(path, sizeof(path), "/api/events?after=%llu", app->social.events_after);
    app->social.event_polls++;
    SDL_UnlockMutex(app->lock);
    akn_http_reply reply;
    if (get(w, path, LOCAL_TIMEOUT_MS, &reply) != 0 || reply.status != 200) {
        akn_http_reply_free(&reply);
        return;
    }
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    akn_events_result r;
    memset(&r, 0, sizeof(r));
    r.next = s->events_after;
    akn_chat_events ce;
    memset(&ce, 0, sizeof(ce));
    if (akn_chat_apply_events(reply.body, reply.len, s->me.id, s->convs, s->thread_conv.id[0] ? s->thread : NULL, &ce) == 0) {
        if (ce.reload_list) s->want |= AKN_WANT_CONVERSATIONS;
        if (ce.reload_thread) s->want |= AKN_WANT_THREAD;
    }
    if (akn_apply_events(reply.body, reply.len, s->friends, s->profile_handle[0] ? &s->profile : NULL, &r) == 0) {
        s->events_after = r.next;
        s->events += (unsigned long)r.events;
        if (r.presence_applied) s->friends_gen++;
        if (r.flags & (AKN_EVF_FRIENDS | AKN_EVF_GAP)) s->want |= AKN_WANT_FRIENDS;
        if ((r.flags & (AKN_EVF_PROFILE | AKN_EVF_GAP)) && s->profile_handle[0]) s->want |= AKN_WANT_PROFILE;
        if (r.flags & (AKN_EVF_GAP | AKN_EVF_STATE)) s->want |= AKN_WANT_ME;
        if (r.flags & (AKN_EVF_STATE | AKN_EVF_ACTIVITY | AKN_EVF_GAP | AKN_EVF_REVOKED)) s->want |= AKN_WANT_STATE;
        if ((r.flags & (AKN_EVF_ACTIVITY | AKN_EVF_GAP)) && s->own_activity_load != AKN_LOAD_NONE) s->want |= AKN_WANT_OWN_ACTIVITY;
        if (r.flags & (AKN_EVF_PARTY | AKN_EVF_INVITATIONS | AKN_EVF_GAP)) s->want |= AKN_WANT_PARTY;
        if (r.flags & (AKN_EVF_INVITATIONS | AKN_EVF_FRIENDS | AKN_EVF_GAP)) s->want |= AKN_WANT_INBOX;
        if (r.flags & (AKN_EVF_GROUPS | AKN_EVF_GAP)) s->want |= AKN_WANT_CONVERSATIONS;
        if ((r.flags & AKN_EVF_GAP) && s->thread_conv.id[0]) s->want |= AKN_WANT_THREAD;
    }
    mark_read_locked(s);
    SDL_UnlockMutex(app->lock);
    if (r.events) akn_logf(&app->log, AKN_LOG_DEBUG, "social.events", "%d events (flags 0x%x)", r.events, r.flags);
    akn_http_reply_free(&reply);
}

/* ---- Messages, party, inbox --------------------------------------------------------------------- */

static void remove_party_conversation(akn_conv_list *l) {
    for (int i = 0; l && i < l->count; i++)
        if (l->items[i].kind == AKN_CONV_PARTY) {
            memmove(&l->items[i], &l->items[i + 1], (size_t)(l->count - i - 1) * sizeof(l->items[0]));
            l->count--;
            i--;
        }
    if (l) akn_conversations_sort(l);
}

/* The party's chat in the conversation list follows the party (lock held). */
static void sync_party_conversation(akn_social *s) {
    if (!s->convs) return;
    if (s->party.present && s->party.conversation_id[0])
        akn_add_party_conversation(s->convs, s->party.conversation_id, s->party.id, s->party.name, s->party.member_count, 0);
    else remove_party_conversation(s->convs);
}

static void load_conversations(worker_ctx *w) {
    akn_app *app = w->app;
    SDL_LockMutex(app->lock);
    if (app->social.convs_load != AKN_LOAD_OK) app->social.convs_load = AKN_LOAD_LOADING;
    SDL_UnlockMutex(app->lock);
    akn_conv_list *list = calloc(1, sizeof(*list));
    akn_party *party = calloc(1, sizeof(*party));
    char err[200] = "";
    const akeno_json *data;
    akeno_json_doc *doc = list && party ? get_proxy(w, "/api/v1/conversations", &data, err, sizeof(err), NULL) : NULL;
    int ok = doc && akn_fill_direct_conversations(data, list) == 0;
    akeno_json_free(doc);
    if (doc && !ok) snprintf(err, sizeof(err), "The conversation list has an unexpected shape.");
    int party_ok = 0;
    if (ok) {
        doc = get_proxy(w, "/api/v1/groups", &data, err, sizeof(err), NULL);
        if (doc) akn_add_group_conversations(data, list); /* the direct conversations stand without the groups */
        akeno_json_free(doc);
        doc = get_proxy(w, "/api/v1/parties/current", &data, err, sizeof(err), NULL);
        party_ok = doc && akn_fill_current_party(data, party) == 0;
        akeno_json_free(doc);
        err[0] = '\0';
    }
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (ok) {
        /* the open conversation's messages are read: they do not count */
        int open = akn_conversations_find(list, s->thread_conv.id);
        if (open >= 0 && strcmp(s->focus_conv, s->thread_conv.id) == 0) list->items[open].unread = 0;
        if (open >= 0 && s->thread_conv.kind != AKN_CONV_PARTY) {
            s->thread_conv.can_send = list->items[open].can_send;
            s->thread_conv.presence = list->items[open].presence;
        }
        free(s->convs);
        s->convs = list;
        list = NULL;
        if (party_ok) {
            s->party = *party;
            s->party_load = AKN_LOAD_OK;
        }
        sync_party_conversation(s);
        s->convs_load = AKN_LOAD_OK;
        s->convs_error[0] = '\0';
    } else {
        if (s->convs_load != AKN_LOAD_OK) s->convs_load = AKN_LOAD_ERROR;
        snprintf(s->convs_error, sizeof(s->convs_error), "%s", err[0] ? err : "Your conversations could not be loaded.");
    }
    int count = s->convs ? s->convs->count : 0, unread = s->convs ? s->convs->unread_total : 0;
    SDL_UnlockMutex(app->lock);
    free(list);
    free(party);
    if (ok) akn_logf(&app->log, AKN_LOG_INFO, "social.messages", "%d conversations, %d unread messages", count, unread);
    else akn_logf(&app->log, AKN_LOG_WARN, "social.messages", "%s", err);
}

/* The open conversation's newest messages. */
static void load_thread(worker_ctx *w) {
    akn_app *app = w->app;
    char conv[40], path[160], err[200] = "", self[40];
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    snprintf(conv, sizeof(conv), "%s", s->thread_conv.id);
    snprintf(self, sizeof(self), "%s", s->me.id);
    if (s->thread_load != AKN_LOAD_OK) s->thread_load = AKN_LOAD_LOADING;
    SDL_UnlockMutex(app->lock);
    if (!conv[0]) return;
    snprintf(path, sizeof(path), "/api/v1/conversations/%s/messages?limit=%d", conv, THREAD_PAGE);
    const akeno_json *data;
    akeno_json_doc *doc = get_proxy(w, path, &data, err, sizeof(err), NULL);
    SDL_LockMutex(app->lock);
    int ok = 0;
    if (strcmp(s->thread_conv.id, conv) == 0 && s->thread) { /* still the conversation on screen */
        ok = doc && akn_fill_thread(data, self, s->thread) == 0;
        if (ok) {
            s->thread_load = AKN_LOAD_OK;
            s->thread_error[0] = '\0';
            mark_read_locked(s);
        } else {
            if (s->thread_load != AKN_LOAD_OK) s->thread_load = AKN_LOAD_ERROR;
            snprintf(s->thread_error, sizeof(s->thread_error), "%s", err[0] ? err : "The messages could not be loaded.");
        }
    }
    int count = s->thread ? s->thread->count : 0;
    SDL_UnlockMutex(app->lock);
    akeno_json_free(doc);
    /* counts only: the log never holds message text or who wrote it */
    if (ok) akn_logf(&app->log, AKN_LOG_INFO, "social.thread", "conversation loaded, %d messages", count);
    else akn_logf(&app->log, AKN_LOG_WARN, "social.thread", "%s", err[0] ? err : "not loaded");
}

static void load_older(worker_ctx *w) {
    akn_app *app = w->app;
    char conv[40], path[200], err[200] = "", self[40];
    long long before;
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    snprintf(conv, sizeof(conv), "%s", s->thread_conv.id);
    snprintf(self, sizeof(self), "%s", s->me.id);
    before = s->thread ? akn_thread_oldest_id(s->thread) : 0;
    int more = s->thread && s->thread->has_more;
    SDL_UnlockMutex(app->lock);
    if (conv[0] && more && before > 0) {
        snprintf(path, sizeof(path), "/api/v1/conversations/%s/messages?before=%lld&limit=%d", conv, before, OLDER_PAGE);
        const akeno_json *data;
        akeno_json_doc *doc = get_proxy(w, path, &data, err, sizeof(err), NULL);
        SDL_LockMutex(app->lock);
        if (doc && strcmp(s->thread_conv.id, conv) == 0 && s->thread) akn_thread_prepend(data, self, s->thread);
        SDL_UnlockMutex(app->lock);
        akeno_json_free(doc);
    }
    SDL_LockMutex(app->lock);
    s->older_loading = 0;
    SDL_UnlockMutex(app->lock);
}

static void load_party(worker_ctx *w) {
    akn_app *app = w->app;
    SDL_LockMutex(app->lock);
    if (app->social.party_load != AKN_LOAD_OK) app->social.party_load = AKN_LOAD_LOADING;
    SDL_UnlockMutex(app->lock);
    akn_party *party = calloc(1, sizeof(*party));
    akn_party_list *joinable = calloc(1, sizeof(*joinable));
    char err[200] = "";
    const akeno_json *data;
    akeno_json_doc *doc = party && joinable ? get_proxy(w, "/api/v1/parties/current", &data, err, sizeof(err), NULL) : NULL;
    int ok = doc && akn_fill_current_party(data, party) == 0;
    akeno_json_free(doc);
    if (doc && !ok) snprintf(err, sizeof(err), "The party has an unexpected shape.");
    int joinable_ok = 0;
    if (ok) {
        doc = get_proxy(w, "/api/v1/parties", &data, err, sizeof(err), NULL);
        joinable_ok = doc && akn_fill_joinable_parties(data, party->present ? party->id : NULL, joinable) == 0;
        akeno_json_free(doc);
        err[0] = '\0';
    }
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (ok) {
        s->party = *party;
        s->party_load = AKN_LOAD_OK;
        s->party_error[0] = '\0';
        if (joinable_ok) {
            free(s->joinable);
            s->joinable = joinable;
            joinable = NULL;
        }
        sync_party_conversation(s);
    } else {
        if (s->party_load != AKN_LOAD_OK) s->party_load = AKN_LOAD_ERROR;
        snprintf(s->party_error, sizeof(s->party_error), "%s", err[0] ? err : "Your party could not be loaded.");
    }
    int present = s->party.present, members = s->party.member_count, others = s->joinable ? s->joinable->count : 0;
    SDL_UnlockMutex(app->lock);
    free(party);
    free(joinable);
    if (ok) akn_logf(&app->log, AKN_LOG_INFO, "social.party", "%s (%d members), %d parties of friends", present ? "in a party" : "no party", members, others);
    else akn_logf(&app->log, AKN_LOG_WARN, "social.party", "%s", err);
}

static void load_inbox(worker_ctx *w) {
    akn_app *app = w->app;
    SDL_LockMutex(app->lock);
    if (app->social.inbox_load != AKN_LOAD_OK) app->social.inbox_load = AKN_LOAD_LOADING;
    SDL_UnlockMutex(app->lock);
    akn_inbox *in = calloc(1, sizeof(*in));
    char err[200] = "";
    const akeno_json *data;
    akeno_json_doc *doc = in ? get_proxy(w, "/api/v1/friends/requests", &data, err, sizeof(err), NULL) : NULL;
    int ok = doc && akn_fill_friend_requests(data, in) == 0;
    akeno_json_free(doc);
    if (ok) {
        doc = get_proxy(w, "/api/v1/invitations", &data, err, sizeof(err), NULL);
        ok = doc && akn_fill_invitations(data, in) == 0;
        akeno_json_free(doc);
    }
    if (ok) {
        doc = get_proxy(w, "/api/v1/notifications?limit=30", &data, err, sizeof(err), NULL);
        if (doc) akn_fill_notifications(data, in); /* requests and invitations stand without them */
        akeno_json_free(doc);
        err[0] = '\0';
    }
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (ok) {
        free(s->inbox);
        s->inbox = in;
        in = NULL;
        s->inbox_load = AKN_LOAD_OK;
        s->inbox_error[0] = '\0';
        if (s->friends) s->friends->incoming_requests = s->inbox->incoming_count;
    } else {
        if (s->inbox_load != AKN_LOAD_OK) s->inbox_load = AKN_LOAD_ERROR;
        snprintf(s->inbox_error, sizeof(s->inbox_error), "%s", err[0] ? err : "Your requests and invitations could not be loaded.");
    }
    int requests = s->inbox ? s->inbox->incoming_count : 0, invitations = s->inbox ? s->inbox->invitation_count : 0;
    int unread = s->inbox ? s->inbox->notifications_unread : 0;
    SDL_UnlockMutex(app->lock);
    free(in);
    if (ok) akn_logf(&app->log, AKN_LOG_INFO, "social.inbox", "%d friend requests, %d invitations, %d unread notifications", requests, invitations, unread);
    else akn_logf(&app->log, AKN_LOG_WARN, "social.inbox", "%s", err);
}

/* Tells the daemon which conversation is on screen (or none). */
static void send_focus(worker_ctx *w) {
    akn_app *app = w->app;
    char conv[40], body[80];
    SDL_LockMutex(app->lock);
    snprintf(conv, sizeof(conv), "%s", app->social.focus_conv);
    app->social.focus_dirty = 0;
    app->social.focus_sent_at = akn_platform_now_ms();
    SDL_UnlockMutex(app->lock);
    if (conv[0]) snprintf(body, sizeof(body), "{\"conversationId\":\"%s\"}", conv);
    else snprintf(body, sizeof(body), "{\"conversationId\":null}");
    akn_http_reply reply;
    call(w, "POST", "/api/native/focus", body, LOCAL_TIMEOUT_MS, &reply);
    akn_http_reply_free(&reply);
}

static const char *action_name(akn_action_kind k) {
    switch (k) {
    case AKN_ACT_SEND: return "send";
    case AKN_ACT_READ: return "read";
    case AKN_ACT_OPEN_DIRECT: return "open-direct";
    case AKN_ACT_PARTY_CREATE: return "party-create";
    case AKN_ACT_PARTY_JOIN: return "party-join";
    case AKN_ACT_PARTY_LEAVE: return "party-leave";
    case AKN_ACT_PARTY_INVITE: return "party-invite";
    case AKN_ACT_INVITATION_ACCEPT: return "invitation-accept";
    case AKN_ACT_INVITATION_DECLINE: return "invitation-decline";
    case AKN_ACT_REQUEST_ACCEPT: return "request-accept";
    case AKN_ACT_REQUEST_DECLINE: return "request-decline";
    case AKN_ACT_NOTIFICATIONS_READ: return "notifications-read";
    case AKN_ACT_PRESENCE: return "presence";
    case AKN_ACT_SETTING: return "setting";
    }
    return "?";
}

/* After an answer: the invitation or request is gone, and so is a party you are now in. */
static void drop_answered_locked(akn_social *s, const akn_action *a) {
    akn_inbox *in = s->inbox;
    for (int i = 0; in && i < in->invitation_count; i++)
        if (strcmp(in->invitations[i].id, a->target) == 0) {
            memmove(&in->invitations[i], &in->invitations[i + 1], (size_t)(in->invitation_count - i - 1) * sizeof(in->invitations[0]));
            in->invitation_count--;
            break;
        }
    for (int i = 0; in && i < in->incoming_count; i++)
        if (strcmp(in->incoming[i].id, a->target) == 0) {
            memmove(&in->incoming[i], &in->incoming[i + 1], (size_t)(in->incoming_count - i - 1) * sizeof(in->incoming[0]));
            in->incoming_count--;
            break;
        }
    akn_party_list *j = s->joinable;
    for (int i = 0; j && i < j->count; i++)
        if (strcmp(j->items[i].id, a->target) == 0 || (s->party.present && strcmp(j->items[i].id, s->party.id) == 0)) {
            memmove(&j->items[i], &j->items[i + 1], (size_t)(j->count - i - 1) * sizeof(j->items[0]));
            j->count--;
            i--;
        }
}

/* The first queued write: made, then its result applied. */
static void do_action(worker_ctx *w) {
    akn_app *app = w->app;
    akn_social *s = &app->social;
    akn_action a;
    SDL_LockMutex(app->lock);
    if (s->action_count == 0) {
        SDL_UnlockMutex(app->lock);
        return;
    }
    a = s->actions[0];
    memmove(&s->actions[0], &s->actions[1], (size_t)(s->action_count - 1) * sizeof(s->actions[0]));
    s->action_count--;
    SDL_UnlockMutex(app->lock);

    char err[200] = "";
    const akeno_json *data = NULL;
    akeno_json_doc *doc = NULL;
    int ok;
    if (a.kind == AKN_ACT_SETTING) { /* the daemon's own setting, not the server */
        akn_http_reply reply;
        ok = call(w, "POST", a.path, a.body, LOCAL_TIMEOUT_MS, &reply) == 0 && reply.status == 200;
        if (!ok) snprintf(err, sizeof(err), "The AKENO daemon did not take the setting (HTTP %d).", reply.status);
        akn_http_reply_free(&reply);
    } else {
        doc = proxy(w, a.method, a.path, a.body[0] ? a.body : NULL, &data, err, sizeof(err), NULL);
        ok = doc != NULL;
    }

    SDL_LockMutex(app->lock);
    s->writes++;
    if (!ok) s->write_errors++;
    if (ok && a.target[0]) drop_answered_locked(s, &a);
    switch (a.kind) {
    case AKN_ACT_SEND:
        if (ok) {
            akn_message m;
            if (akn_parse_message(akeno_json_get(data, "message"), s->me.id, &m) == 0) {
                if (s->thread && strcmp(s->thread_conv.id, a.conversation_id) == 0) akn_thread_add(s->thread, &m);
                int i = akn_conversations_find(s->convs, a.conversation_id);
                if (i >= 0) {
                    akn_conversation *c = &s->convs->items[i];
                    akn_chat_preview(m.body, c->preview, sizeof(c->preview));
                    c->preview_mine = 1;
                    if (m.id > c->last_message_id) c->last_message_id = m.id;
                    if (m.created_at) c->last_at = m.created_at;
                    akn_conversations_sort(s->convs);
                } else {
                    s->want |= AKN_WANT_CONVERSATIONS;
                }
                mark_read_locked(s);
            }
        } else {
            if (s->thread && strcmp(s->thread_conv.id, a.conversation_id) == 0) akn_thread_set_state(s->thread, a.client_id, AKN_MSG_FAILED);
            set_result_locked(s, 1, "Not sent: %s Press Triangle to try again.", err);
        }
        break;
    case AKN_ACT_READ:
        if (!ok) s->read_sent_id = 0; /* try again with the next message */
        break;
    case AKN_ACT_OPEN_DIRECT:
        if (ok && akn_parse_direct_conversation(data, &s->thread_conv) == 0) {
            if (s->thread) memset(s->thread, 0, sizeof(*s->thread));
            if (s->thread) snprintf(s->thread->conversation_id, sizeof(s->thread->conversation_id), "%s", s->thread_conv.id);
            s->thread_load = AKN_LOAD_LOADING;
            s->read_sent_id = 0;
            s->open_chat_ready = 1;
            s->want |= AKN_WANT_THREAD | AKN_WANT_CONVERSATIONS;
        } else {
            set_result_locked(s, 1, "The conversation with %s could not be opened: %s", a.label, ok ? "unexpected answer." : err);
        }
        break;
    case AKN_ACT_PARTY_CREATE:
    case AKN_ACT_PARTY_JOIN:
        if (ok) {
            akn_party p;
            if (akn_fill_party(data, &p) == 0) {
                s->party = p;
                s->party_load = AKN_LOAD_OK;
                sync_party_conversation(s);
            }
            set_result_locked(s, 0, a.kind == AKN_ACT_PARTY_CREATE ? "Party created. Invite friends with Cross on \"Invite friends\"." : "You joined %s.",
                              a.label);
        } else {
            set_result_locked(s, 1, "%s", err);
        }
        s->want |= AKN_WANT_PARTY | AKN_WANT_CONVERSATIONS;
        break;
    case AKN_ACT_PARTY_LEAVE:
        if (ok) {
            memset(&s->party, 0, sizeof(s->party));
            sync_party_conversation(s);
            if (s->thread_conv.kind == AKN_CONV_PARTY) s->thread_conv.can_send = 0;
        }
        set_result_locked(s, !ok, ok ? "You left the party." : "%s", err);
        s->want |= AKN_WANT_PARTY | AKN_WANT_CONVERSATIONS;
        break;
    case AKN_ACT_PARTY_INVITE:
        set_result_locked(s, !ok, ok ? "Invitation sent to %s." : "%s could not be invited: %s", a.label, err);
        s->want |= AKN_WANT_PARTY;
        break;
    case AKN_ACT_INVITATION_ACCEPT:
        set_result_locked(s, !ok, ok ? "Accepted: %s." : "Could not accept: %s", ok ? a.label : err);
        s->want |= AKN_WANT_INBOX | AKN_WANT_PARTY | AKN_WANT_CONVERSATIONS;
        break;
    case AKN_ACT_INVITATION_DECLINE:
        set_result_locked(s, !ok, ok ? "Declined: %s." : "Could not decline: %s", ok ? a.label : err);
        s->want |= AKN_WANT_INBOX | AKN_WANT_PARTY;
        break;
    case AKN_ACT_REQUEST_ACCEPT:
        set_result_locked(s, !ok, ok ? "You and %s are now friends." : "Could not accept: %s", ok ? a.label : err);
        s->want |= AKN_WANT_INBOX | AKN_WANT_FRIENDS;
        break;
    case AKN_ACT_REQUEST_DECLINE:
        set_result_locked(s, !ok, ok ? "Friend request from %s declined." : "Could not decline: %s", ok ? a.label : err);
        s->want |= AKN_WANT_INBOX | AKN_WANT_FRIENDS;
        break;
    case AKN_ACT_NOTIFICATIONS_READ:
        if (ok && s->inbox) {
            s->inbox->notifications_unread = 0;
            for (int i = 0; i < s->inbox->notification_count; i++) s->inbox->notifications[i].unread = 0;
        }
        if (!ok) set_result_locked(s, 1, "%s", err);
        s->want |= AKN_WANT_INBOX;
        break;
    case AKN_ACT_PRESENCE:
        if (ok) snprintf(s->presence_pref, sizeof(s->presence_pref), "%.15s", a.label);
        set_result_locked(s, !ok, ok ? "Your status is now %s." : "%s", ok ? (strcmp(a.label, "dnd") == 0 ? "Busy" : strcmp(a.label, "away") == 0 ? "Away"
                                                                                : strcmp(a.label, "invisible") == 0 ? "Invisible"
                                                                                                                      : "Online")
                                                                         : err);
        s->want |= AKN_WANT_ME;
        break;
    case AKN_ACT_SETTING:
        set_result_locked(s, !ok, ok ? "%s" : "%s", ok ? a.label : err);
        s->want |= AKN_WANT_STATE;
        break;
    }
    SDL_UnlockMutex(app->lock);
    akeno_json_free(doc);
    /* the kind and the outcome only: no IDs, names or text */
    akn_logf(&app->log, ok ? AKN_LOG_INFO : AKN_LOG_WARN, "social.write", "%s: %s", action_name(a.kind), ok ? "done" : "failed");
}

/* ---- Worker -------------------------------------------------------------------------------------- */

static int social_worker(void *arg) {
    worker_ctx *w = arg;
    akn_app *app = w->app;
    akn_social *s = &app->social;
    uint64_t next_connect = 0, next_events = 0, next_state = 0;
    int was_ready = 0;
    SDL_LockMutex(app->lock);
    while (!s->stop) {
        uint64_t now = akn_platform_now_ms();
        int ready = s->ipc == AKN_IPC_READY;
        if (was_ready && !ready) next_connect = 0; /* lost it just now: try again at once, then every few seconds */
        was_ready = ready;
        if (s->want & AKN_WANT_RECONNECT) {
            s->want &= ~(unsigned)AKN_WANT_RECONNECT;
            next_connect = 0;
        }
        void (*job)(worker_ctx *) = NULL;
        int focus_due = s->focus_dirty || (s->focus_conv[0] && now - s->focus_sent_at >= FOCUS_MS);
        if (!ready) {
            if (now >= next_connect) {
                job = connect_daemon;
                next_connect = now + RECONNECT_MS;
                next_events = now + EVENTS_MS;
                next_state = now + STATE_MS;
            }
        } else if (s->action_count > 0) {
            job = do_action; /* writes first: a message on its way matters most */
        } else if (focus_due && s->daemon.ipc >= 2) {
            job = send_focus;
        } else if ((s->want & AKN_WANT_ME) && !s->me.id[0]) {
            s->want &= ~(unsigned)AKN_WANT_ME;
            job = load_me; /* your ID decides which messages are yours */
        } else if (s->want & AKN_WANT_STATE) {
            s->want &= ~(unsigned)AKN_WANT_STATE;
            job = load_state;
            next_state = now + STATE_MS;
        } else if (s->want & AKN_WANT_THREAD) {
            s->want &= ~(unsigned)AKN_WANT_THREAD;
            job = load_thread;
        } else if (s->want & AKN_WANT_OLDER) {
            s->want &= ~(unsigned)AKN_WANT_OLDER;
            job = load_older;
        } else if (s->want & AKN_WANT_PROFILE) {
            s->want &= ~(unsigned)AKN_WANT_PROFILE;
            job = load_profile;
        } else if (s->want & AKN_WANT_FRIENDS) {
            s->want &= ~(unsigned)AKN_WANT_FRIENDS;
            job = load_friends;
        } else if (s->want & AKN_WANT_CONVERSATIONS) {
            s->want &= ~(unsigned)AKN_WANT_CONVERSATIONS;
            job = load_conversations;
        } else if (s->want & AKN_WANT_PARTY) {
            s->want &= ~(unsigned)AKN_WANT_PARTY;
            job = load_party;
        } else if (s->want & AKN_WANT_INBOX) {
            s->want &= ~(unsigned)AKN_WANT_INBOX;
            job = load_inbox;
        } else if (s->want & AKN_WANT_OWN_ACTIVITY) {
            s->want &= ~(unsigned)AKN_WANT_OWN_ACTIVITY;
            job = load_own_activity;
        } else if (s->want & AKN_WANT_ME) {
            s->want &= ~(unsigned)AKN_WANT_ME;
            job = load_me;
        } else if (now >= next_events) {
            job = poll_events;
            next_events = now + EVENTS_MS;
        } else if (now >= next_state) {
            job = load_state;
            next_state = now + STATE_MS;
        }
        if (job) {
            s->busy = 1;
            SDL_UnlockMutex(app->lock);
            job(w);
            SDL_LockMutex(app->lock);
            s->busy = 0;
            continue;
        }
        SDL_CondWaitTimeout(app->social_cond, app->lock, 100);
    }
    s->done = 1;
    SDL_UnlockMutex(app->lock);
    memset(w->key, 0, sizeof(w->key));
    free(w);
    return 0;
}

int akn_social_start(akn_app *app) {
    app->social.thread = calloc(1, sizeof(*app->social.thread));
    if (!app->social.thread) return -1;
    worker_ctx *w = calloc(1, sizeof(*w));
    app->social_cond = SDL_CreateCond();
    if (!w || !app->social_cond) {
        free(w);
        return -1;
    }
    w->app = app;
    app->social_thread = SDL_CreateThread(social_worker, "akeno-social", w);
    if (!app->social_thread) {
        free(w);
        akn_logf(&app->log, AKN_LOG_ERROR, "social.thread", "could not start: %s", SDL_GetError());
        return -1;
    }
    return 0;
}

int akn_social_stop(akn_app *app) {
    if (!app->social_thread) return 1;
    SDL_LockMutex(app->lock);
    app->social.stop = 1;
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
    /* A request in flight ends within its timeout; do not hold up the exit for it. */
    for (int i = 0; i < 50; i++) {
        SDL_LockMutex(app->lock);
        int done = app->social.done;
        SDL_UnlockMutex(app->lock);
        if (done) {
            SDL_WaitThread(app->social_thread, NULL);
            app->social_thread = NULL;
            return 1;
        }
        SDL_Delay(10);
    }
    SDL_DetachThread(app->social_thread);
    app->social_thread = NULL;
    return 0;
}

void akn_social_want(akn_app *app, unsigned bits) {
    SDL_LockMutex(app->lock);
    app->social.want |= bits;
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
}

void akn_social_open_profile(akn_app *app, const char *handle, int own) {
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (strcmp(s->profile_handle, handle) != 0) {
        memset(&s->profile, 0, sizeof(s->profile));
        memset(&s->profile_activity, 0, sizeof(s->profile_activity));
        s->profile_activity_load = AKN_LOAD_NONE;
    }
    snprintf(s->profile_handle, sizeof(s->profile_handle), "%s", handle);
    s->profile_own = own;
    s->profile_load = s->profile.online_id[0] ? AKN_LOAD_OK : AKN_LOAD_LOADING;
    s->want |= AKN_WANT_PROFILE;
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
}

/* ---- Main thread: conversations and writes ------------------------------------------------------- */

void akn_social_open_chat(akn_app *app, const akn_conversation *conv) {
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (strcmp(s->thread_conv.id, conv->id) != 0) {
        if (s->thread) {
            memset(s->thread, 0, sizeof(*s->thread));
            snprintf(s->thread->conversation_id, sizeof(s->thread->conversation_id), "%s", conv->id);
        }
        s->thread_load = AKN_LOAD_LOADING;
        s->read_sent_id = 0;
        s->older_loading = 0;
    }
    s->thread_conv = *conv;
    s->want |= AKN_WANT_THREAD;
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
}

int akn_social_can_write(akn_app *app) {
    SDL_LockMutex(app->lock);
    int ok = writes_allowed(&app->social);
    SDL_UnlockMutex(app->lock);
    return ok;
}

int akn_social_act(akn_app *app, const akn_action *action) {
    SDL_LockMutex(app->lock);
    int rc = enqueue_locked(&app->social, action);
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
    return rc;
}

static int queue_send_locked(akn_social *s, const char *client_id, const char *body) {
    akn_action a;
    memset(&a, 0, sizeof(a));
    a.kind = AKN_ACT_SEND;
    snprintf(a.method, sizeof(a.method), "POST");
    snprintf(a.path, sizeof(a.path), "/api/v1/conversations/%s/messages", s->thread_conv.id);
    snprintf(a.client_id, sizeof(a.client_id), "%s", client_id);
    snprintf(a.conversation_id, sizeof(a.conversation_id), "%s", s->thread_conv.id);
    if (akn_message_body_json(body, client_id, a.body, sizeof(a.body)) != 0) return -1;
    return enqueue_locked(s, &a);
}

int akn_social_send(akn_app *app, const char *body) {
    if (!akn_message_valid(body)) return -1;
    uint8_t rnd[16];
    if (akeno_random_bytes(rnd, sizeof(rnd)) != 0) return -1;
    char client_id[37];
    akn_uuid_v4(rnd, client_id);
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int rc = -1;
    if (s->thread && s->thread_conv.id[0] && s->thread_conv.can_send && writes_allowed(s) && s->action_count < AKN_ACTIONS_MAX &&
        akn_thread_add_pending(s->thread, client_id, s->me.id, s->me.display_name[0] ? s->me.display_name : "You", body,
                               akn_platform_unix_time()) == 0)
        rc = queue_send_locked(s, client_id, body);
    if (rc != 0 && s->thread) akn_thread_set_state(s->thread, client_id, AKN_MSG_FAILED);
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
    return rc;
}

int akn_social_retry_failed(akn_app *app) {
    int n = 0;
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    for (int i = 0; s->thread && i < s->thread->count; i++) {
        akn_message *m = &s->thread->items[i];
        if (m->id != 0 || m->state != AKN_MSG_FAILED) continue;
        if (queue_send_locked(s, m->client_id, m->body) == 0) {
            m->state = AKN_MSG_SENDING;
            n++;
        }
    }
    SDL_CondSignal(app->social_cond);
    SDL_UnlockMutex(app->lock);
    return n;
}

int akn_social_inbox_rows(const akn_social *s, akn_inbox_row *out, int max) {
    int n = 0;
    const akn_inbox *in = s->inbox;
    for (int i = 0; in && i < in->incoming_count && n < max; i++) out[n++] = (akn_inbox_row){ AKN_IROW_REQUEST, i };
    for (int i = 0; in && i < in->invitation_count && n < max; i++) out[n++] = (akn_inbox_row){ AKN_IROW_INVITATION, i };
    if (in && in->notification_count && n < max) out[n++] = (akn_inbox_row){ AKN_IROW_NOTIFICATIONS, 0 };
    return n;
}
