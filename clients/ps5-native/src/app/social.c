/*
 * The social worker (Phase 2.1): reads friends, profiles and game activity from the AKENO daemon's
 * native interface on 127.0.0.1 (docs/PS5_NATIVE_PHASE2.md) and follows the daemon's event ring.
 * GET only: nothing is changed on the server or in the daemon. The daemon keeps the only realtime
 * session, the notifications and the game detection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akeno/json.h"
#include "app.h"
#include "ipc.h"
#include "net.h"

#define RECONNECT_MS 3000
#define EVENTS_MS 1000
#define STATE_MS 5000
/* The daemon answers one request at a time, so a local answer can wait behind a proxied one. */
#define LOCAL_TIMEOUT_MS 5000    /* daemon-local answers (hello, state, events) */
#define PROXY_TIMEOUT_MS 15000   /* through the daemon to the server */
#define REPLY_MAX (1024 * 1024)

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
static int get(worker_ctx *w, const char *path, int timeout_ms, akn_http_reply *reply) {
    akn_app *app = w->app;
    SDL_LockMutex(app->lock);
    app->social.requests++;
    SDL_UnlockMutex(app->lock);
    akn_loopback_get(app->platform.daemon_port, path, w->key, timeout_ms, REPLY_MAX, reply);
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

/* Reads a proxied server answer. Returns the document (caller frees) with *data set, or NULL with
 * a readable error in err. */
static akeno_json_doc *get_proxy(worker_ctx *w, const char *path, const akeno_json **data, char *err, size_t err_len, int *server_status) {
    akn_http_reply reply;
    *data = NULL;
    if (server_status) *server_status = 0;
    if (get(w, path, PROXY_TIMEOUT_MS, &reply) != 0) {
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
    else if (res.status == 0) snprintf(err, err_len, "The daemon could not reach the AKENO server (%s).", res.code[0] ? res.code : "offline");
    else snprintf(err, err_len, "The server answered %d%s%s.", res.status, res.message[0] ? ": " : "", res.message);
    akeno_json_free(doc);
    *data = NULL;
    SDL_LockMutex(w->app->lock);
    w->app->social.errors++;
    SDL_UnlockMutex(w->app->lock);
    return NULL;
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
    s->want |= AKN_WANT_STATE | AKN_WANT_FRIENDS | AKN_WANT_ME;
    if (s->profile_handle[0]) s->want |= AKN_WANT_PROFILE;
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
    if (akn_apply_events(reply.body, reply.len, s->friends, s->profile_handle[0] ? &s->profile : NULL, &r) == 0) {
        s->events_after = r.next;
        s->events += (unsigned long)r.events;
        if (r.presence_applied) s->friends_gen++;
        if (r.flags & (AKN_EVF_FRIENDS | AKN_EVF_GAP)) s->want |= AKN_WANT_FRIENDS;
        if ((r.flags & (AKN_EVF_PROFILE | AKN_EVF_GAP)) && s->profile_handle[0]) s->want |= AKN_WANT_PROFILE;
        if (r.flags & (AKN_EVF_GAP | AKN_EVF_STATE)) s->want |= AKN_WANT_ME;
        if (r.flags & (AKN_EVF_STATE | AKN_EVF_ACTIVITY | AKN_EVF_GAP | AKN_EVF_REVOKED)) s->want |= AKN_WANT_STATE;
        if ((r.flags & (AKN_EVF_ACTIVITY | AKN_EVF_GAP)) && s->own_activity_load != AKN_LOAD_NONE) s->want |= AKN_WANT_OWN_ACTIVITY;
    }
    SDL_UnlockMutex(app->lock);
    if (r.events) akn_logf(&app->log, AKN_LOG_DEBUG, "social.events", "%d events (flags 0x%x)", r.events, r.flags);
    akn_http_reply_free(&reply);
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
        if (!ready) {
            if (now >= next_connect) {
                job = connect_daemon;
                next_connect = now + RECONNECT_MS;
                next_events = now + EVENTS_MS;
                next_state = now + STATE_MS;
            }
        } else if (s->want & AKN_WANT_STATE) {
            s->want &= ~(unsigned)AKN_WANT_STATE;
            job = load_state;
            next_state = now + STATE_MS;
        } else if (s->want & AKN_WANT_PROFILE) {
            s->want &= ~(unsigned)AKN_WANT_PROFILE;
            job = load_profile;
        } else if (s->want & AKN_WANT_FRIENDS) {
            s->want &= ~(unsigned)AKN_WANT_FRIENDS;
            job = load_friends;
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
