/*
 * Game activity worker: detects the game running on this console and reports it to the AKENO
 * server (POST /activity/heartbeat while it runs, POST /activity/stop when it ends).
 *
 *   scan       every 10 s: list processes (platform layer, read-only) and classify them
 *   debounce   a game counts as started or ended after two consecutive scans agree
 *   heartbeat  immediately on a new game, then at the interval the server asks for (60 s)
 *   stop       when the game ends, when sharing is turned off and when AKENO exits
 *
 * The server owns all timestamps. If heartbeats stop (rest mode, network loss, a crash), the
 * server ends the session at the last heartbeat. Only this device's own credential is used, and
 * it needs the "activity:write" permission granted when the console was paired.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_internal.h"
#include "client_internal.h"
#include "transport.h"

#define DEFAULT_SCAN_MS 10000
#define DEFAULT_HEARTBEAT_MS 60000
#define CONFIRM_SCANS 2
#define MAX_PROCS 1024
#define RETRY_MS 15000
#define RATE_LIMITED_MS 60000
#define DISABLED_RETRY_MS (5 * 60 * 1000)
#define NAME_CACHE 8

typedef struct {
    char title_id[AKENO_TITLE_ID_MAX];
    char name[AKENO_TITLE_NAME_MAX];
    char source[160];
} name_entry;

typedef struct {
    akeno_app *app;
    akeno_client *client;
    akeno_proc_info *procs;
    akeno_game_debounce debounce;
    name_entry names[NAME_CACHE];
    unsigned name_next;
    const name_entry *current_name;
    int heartbeat_ms;
    long long next_scan;
    long long next_beat;
    int reported;       /* The server has an open session reported by this device. */
    int scope_denied;   /* Paired without "activity:write". */
    int server_off;     /* Turned off in the website's privacy settings. */
    int net_problem;
    int signed_out;
    int unsupported;
    char unsupported_reason[120];
} act_ctx;

static int act_should_stop(akeno_app *app) { return app->stop || app->act_stop || app_exit_requested(); }

/* ---- State shown on the page -------------------------------------------------------------- */

void app_activity_json(const akeno_app *app, akeno_buf *b, int diagnostics) {
    const app_activity *a = &app->act;
    long long now = akeno_now_ms();
    akeno_buf_printf(b, "{\"enabled\":%s,\"state\":", app->activity_enabled ? "true" : "false");
    akeno_buf_append_json_string(b, a->state);
    akeno_buf_append_str(b, ",\"detail\":");
    akeno_buf_append_json_string(b, a->detail);
    akeno_buf_append_str(b, ",\"titleId\":");
    if (a->title_id[0]) akeno_buf_append_json_string(b, a->title_id);
    else akeno_buf_append_str(b, "null");
    akeno_buf_append_str(b, ",\"name\":");
    if (a->name[0]) akeno_buf_append_json_string(b, a->name);
    else akeno_buf_append_str(b, "null");
    akeno_buf_append_str(b, ",\"startedAt\":");
    if (a->started_at[0]) akeno_buf_append_json_string(b, a->started_at);
    else akeno_buf_append_str(b, "null");
    akeno_buf_printf(b, ",\"sinceSeconds\":%lld", a->since ? (now - a->since) / 1000 : 0);
    if (diagnostics) {
        const akeno_game_scan *s = &a->scan;
        akeno_buf_append_str(b, ",\"method\":");
        akeno_buf_append_json_string(b, app->platform.game_detection);
        akeno_buf_append_str(b, ",\"nameSource\":");
        akeno_buf_append_json_string(b, a->name_source);
        akeno_buf_printf(b, ",\"scanIntervalMs\":%d,\"heartbeatIntervalMs\":%d,\"heartbeatsOk\":%d,\"heartbeatsFailed\":%d", a->scan_ms,
                         a->heartbeat_ms, a->heartbeats_ok, a->heartbeats_failed);
        akeno_buf_printf(b, ",\"lastScanSecondsAgo\":%lld,\"lastHeartbeatSecondsAgo\":%lld",
                         a->last_scan_at ? (now - a->last_scan_at) / 1000 : -1, a->last_heartbeat_at ? (now - a->last_heartbeat_at) / 1000 : -1);
        akeno_buf_append_str(b, ",\"lastScan\":{\"result\":");
        akeno_buf_append_json_string(b, a->last_scan_at ? akeno_scan_kind_name(s->kind) : "not yet");
        akeno_buf_printf(b, ",\"processes\":%d,\"appProcesses\":%d,\"stoppedGames\":%d,\"candidates\":[", s->processes, s->app_processes,
                         s->stopped_games);
        for (int i = 0; i < s->candidates && i < AKENO_SCAN_MAX_CANDIDATES; i++) {
            if (i) akeno_buf_append_str(b, ",");
            akeno_buf_append_json_string(b, s->candidate_ids[i]);
        }
        akeno_buf_append_str(b, "],\"layout\":");
        akeno_buf_append_json_string(b, akeno_layout_name(s->layout));
        akeno_buf_printf(b, ",\"layoutVotes\":{\"offset20\":%d,\"offset16\":%d},\"probes\":[", s->layout_votes_ps, s->layout_votes_alt);
        for (int i = 0; i < s->probe_count && i < AKENO_SCAN_PROBES; i++) {
            if (i) akeno_buf_append_str(b, ",");
            akeno_buf_append_json_string(b, s->probes[i]);
        }
        akeno_buf_append_str(b, "],\"error\":");
        akeno_buf_append_json_string(b, s->error);
        akeno_buf_append_str(b, "},\"focus\":\"not determined: a running game process is reported, not the application in the foreground\"");
    }
    akeno_buf_append_str(b, "}");
}

/* Derives the visible state from the worker's flags and tells the page when it changes. */
static void publish(act_ctx *c) {
    akeno_app *app = c->app;
    const char *state, *detail;
    if (c->signed_out) {
        state = "signed-out", detail = "This console is no longer paired.";
    } else if (c->scope_denied) {
        state = "permission-missing";
        detail = "This console was paired without permission to share game activity. Unpair it in Settings and pair it again to allow it.";
    } else if (c->server_off) {
        state = "disabled-on-server", detail = "Game activity is turned off in your privacy settings on the AKENO NETWORK website.";
    } else if (c->debounce.current[0]) {
        state = "playing";
        detail = c->net_problem ? "The AKENO server could not be reached; retrying." : c->reported ? "Shared with AKENO NETWORK. Your privacy settings decide who sees it."
                                                                                       : "Detected; sharing with AKENO NETWORK.";
    } else if (c->unsupported) {
        state = "unsupported", detail = c->unsupported_reason;
    } else {
        state = "idle", detail = "No game is running.";
    }
    akeno_buf json;
    akeno_buf_init(&json, 4096);
    pthread_mutex_lock(&app->lock);
    int changed = strcmp(app->act.state, state) != 0 || strcmp(app->act.detail, detail) != 0;
    app_copy_str(app->act.state, sizeof(app->act.state), state);
    app_copy_str(app->act.detail, sizeof(app->act.detail), detail);
    if (changed) app_activity_json(app, &json, 0);
    pthread_mutex_unlock(&app->lock);
    if (changed && json.len) app_push_event(app, "activity.local", json.data);
    akeno_buf_free(&json);
}

/* ---- Server calls ------------------------------------------------------------------------- */

static void send_stop(act_ctx *c, const char *reason) {
    if (!c->reported) return;
    c->reported = 0;
    akeno_buf body;
    akeno_buf_init(&body, 256);
    akeno_buf_append_str(&body, "{\"reason\":");
    akeno_buf_append_json_string(&body, reason);
    akeno_buf_append_str(&body, "}");
    akeno_response response;
    akeno_status status = akeno_request(c->client, "POST", "/activity/stop", body.data, &response);
    akeno_buf_free(&body);
    app_log(c->app, status == AKENO_OK ? AKENO_LOG_INFO : AKENO_LOG_WARN, "activity.stop", "Reported the end of the game session (%s): %s.",
            reason, akeno_status_name(status));
    akeno_response_free(&response);
}

static void heartbeat(act_ctx *c) {
    akeno_app *app = c->app;
    akeno_buf body;
    akeno_buf_init(&body, 1024);
    akeno_buf_append_str(&body, "{\"titleId\":");
    akeno_buf_append_json_string(&body, c->debounce.current);
    if (c->current_name && c->current_name->name[0]) {
        akeno_buf_append_str(&body, ",\"titleName\":");
        akeno_buf_append_json_string(&body, c->current_name->name);
    }
    akeno_buf_append_str(&body, ",\"platform\":\"PS5\"}");
    akeno_response response;
    akeno_status status = akeno_request(c->client, "POST", "/activity/heartbeat", body.data, &response);
    akeno_buf_free(&body);
    long long now = akeno_now_ms();
    if (status == AKENO_OK) {
        const akeno_json *root = akeno_response_json(&response);
        const akeno_json *session = akeno_json_get(root, "session");
        int64_t interval = 0;
        if (!app->opt.activity_heartbeat_ms && akeno_json_int64(akeno_json_get(root, "heartbeatIntervalSeconds"), &interval) == 0 &&
            interval >= 15 && interval <= 300)
            c->heartbeat_ms = (int)interval * 1000;
        if (akeno_json_get_bool(session, "created", 0))
            app_log(app, AKENO_LOG_INFO, "activity.session_started", "The server started a game session for %s.", c->debounce.current);
        pthread_mutex_lock(&app->lock);
        app_copy_str(app->act.started_at, sizeof(app->act.started_at), akeno_json_get_string(session, "startedAt", ""));
        app->act.heartbeats_ok++;
        app->act.last_heartbeat_at = now;
        app->act.heartbeat_ms = c->heartbeat_ms;
        pthread_mutex_unlock(&app->lock);
        c->reported = 1;
        c->server_off = 0;
        c->net_problem = 0;
        c->next_beat = now + c->heartbeat_ms;
    } else {
        pthread_mutex_lock(&app->lock);
        app->act.heartbeats_failed++;
        pthread_mutex_unlock(&app->lock);
        if (status == AKENO_ERR_FORBIDDEN && strcmp(response.error_code, "DEVICE_SCOPE_DENIED") == 0) {
            c->scope_denied = 1;
            c->reported = 0;
        } else if (strcmp(response.error_code, "ACTIVITY_TRACKING_DISABLED") == 0) {
            c->server_off = 1;
            c->reported = 0;
            c->next_beat = now + (app->opt.activity_heartbeat_ms ? app->opt.activity_heartbeat_ms * 3 : DISABLED_RETRY_MS);
        } else if (status == AKENO_ERR_UNAUTHORIZED) {
            c->signed_out = 1;
            c->reported = 0;
        } else if (status == AKENO_ERR_RATE_LIMITED) {
            c->next_beat = now + RATE_LIMITED_MS;
        } else if (response.http_status >= 400 && response.http_status < 500) {
            c->next_beat = now + c->heartbeat_ms; /* E.g. a rejected name; try again later, unchanged. */
        } else {
            c->net_problem = 1;
            c->next_beat = now + (c->heartbeat_ms < RETRY_MS ? c->heartbeat_ms : RETRY_MS);
        }
        app_log(app, AKENO_LOG_WARN, "activity.heartbeat_failed", "%s (HTTP %d %s)", akeno_status_name(status), response.http_status,
                response.error_code);
    }
    akeno_response_free(&response);
}

/* ---- Detection ---------------------------------------------------------------------------- */

static const name_entry *resolve_name(act_ctx *c, const char *title_id) {
    for (int i = 0; i < NAME_CACHE; i++)
        if (strcmp(c->names[i].title_id, title_id) == 0) return &c->names[i];
    name_entry *e = &c->names[c->name_next++ % NAME_CACHE];
    memset(e, 0, sizeof(*e));
    app_copy_str(e->title_id, sizeof(e->title_id), title_id);
    const char *root = akeno_platform_metadata_root();
    if (root && akeno_game_meta_lookup(root, title_id, e->name, sizeof(e->name), e->source, sizeof(e->source)) == 0)
        app_log(c->app, AKENO_LOG_INFO, "activity.name", "%s is \"%s\" (from %s).", title_id, e->name, e->source);
    else
        app_log(c->app, AKENO_LOG_INFO, "activity.name", "No local name for %s; the server may know it.", title_id);
    return e;
}

static void scan(act_ctx *c) {
    akeno_app *app = c->app;
    size_t total = 0;
    char err[120] = "";
    int n = akeno_platform_list_apps(c->procs, MAX_PROCS, &total, err, sizeof(err));
    akeno_game_scan result;
    if (n >= 0 && total > (size_t)n) {
        n = -1; /* A truncated list could hide a second game: no claim. */
        snprintf(err, sizeof(err), "More than %d processes; the list was not evaluated.", MAX_PROCS);
    }
    if (n < 0) {
        memset(&result, 0, sizeof(result));
        result.kind = AKENO_SCAN_FAILED;
        app_copy_str(result.error, sizeof(result.error), err);
    } else {
        akeno_game_classify(c->procs, (size_t)n, (int)getpid(), &result);
        result.processes = (int)total;
        app_copy_str(result.error, sizeof(result.error), err);
    }
    if (n < 0 && !c->unsupported) app_log(app, AKENO_LOG_WARN, "activity.scan_failed", "%s", err);
    c->unsupported = n < 0;
    app_copy_str(c->unsupported_reason, sizeof(c->unsupported_reason), err);

    long long now = akeno_now_ms();
    pthread_mutex_lock(&app->lock);
    app->act.scan = result;
    app->act.last_scan_at = now;
    pthread_mutex_unlock(&app->lock);

    if (!akeno_game_debounce_update(&c->debounce, &result, CONFIRM_SCANS)) return;
    if (c->debounce.current[0]) {
        c->current_name = resolve_name(c, c->debounce.current);
        app_log(app, AKENO_LOG_INFO, "activity.detected", "Game running: %s%s%s%s.", c->debounce.current, c->current_name->name[0] ? " (" : "",
                c->current_name->name, c->current_name->name[0] ? ")" : "");
        pthread_mutex_lock(&app->lock);
        app_copy_str(app->act.title_id, sizeof(app->act.title_id), c->debounce.current);
        app_copy_str(app->act.name, sizeof(app->act.name), c->current_name->name);
        app_copy_str(app->act.name_source, sizeof(app->act.name_source), c->current_name->source);
        app->act.started_at[0] = '\0';
        app->act.since = now;
        pthread_mutex_unlock(&app->lock);
        c->next_beat = now; /* Report a new game (or a switch) right away. */
    } else {
        app_log(app, AKENO_LOG_INFO, "activity.ended", "No game is running any more.");
        c->current_name = NULL;
        send_stop(c, "closed");
        pthread_mutex_lock(&app->lock);
        app->act.title_id[0] = app->act.name[0] = app->act.name_source[0] = app->act.started_at[0] = '\0';
        app->act.since = 0;
        pthread_mutex_unlock(&app->lock);
    }
}

static void *act_main(void *arg) {
    akeno_app *app = arg;
    act_ctx c;
    memset(&c, 0, sizeof(c));
    c.app = app;
    c.heartbeat_ms = app->opt.activity_heartbeat_ms > 0 ? app->opt.activity_heartbeat_ms : DEFAULT_HEARTBEAT_MS;
    int scan_ms = app->opt.activity_scan_ms > 0 ? app->opt.activity_scan_ms : DEFAULT_SCAN_MS;
    pthread_mutex_lock(&app->lock);
    app->act.scan_ms = scan_ms;
    app->act.heartbeat_ms = c.heartbeat_ms;
    pthread_mutex_unlock(&app->lock);

    c.procs = calloc(MAX_PROCS, sizeof(*c.procs));
    akeno_status status = c.procs ? app_make_client(app, &c.client) : AKENO_ERR_NO_MEMORY;
    if (status == AKENO_OK && !akeno_client_is_paired(c.client)) status = AKENO_ERR_NOT_PAIRED;
    if (status != AKENO_OK) {
        app_log(app, AKENO_LOG_WARN, "activity.not_started", "Game activity could not start (%s).", akeno_status_name(status));
        c.signed_out = status == AKENO_ERR_NOT_PAIRED;
    } else {
        app_log(app, AKENO_LOG_INFO, "activity.started", "Game detection every %d ms (%s).", scan_ms, app->platform.game_detection);
        publish(&c);
        while (!act_should_stop(app) && !c.signed_out) {
            long long now = akeno_now_ms();
            if (now >= c.next_scan) {
                scan(&c);
                c.next_scan = now + scan_ms;
            }
            if (c.debounce.current[0] && !c.scope_denied && akeno_now_ms() >= c.next_beat) heartbeat(&c);
            publish(&c);
            struct timespec pause = { 0, 100 * 1000 * 1000 };
            nanosleep(&pause, NULL);
        }
        send_stop(&c, "shutdown");
    }
    akeno_client_destroy(c.client);
    free(c.procs);
    pthread_mutex_lock(&app->lock);
    if (c.signed_out) app->act_wanted = 0; /* Restarted when the console is paired again. */
    app->act.title_id[0] = app->act.name[0] = app->act.started_at[0] = '\0';
    app->act.since = 0;
    app->act_thread_done = 1;
    pthread_mutex_unlock(&app->lock);
    return NULL;
}

/* ---- Main-thread control ------------------------------------------------------------------ */

void app_activity_init(akeno_app *app) {
    app_copy_str(app->act.state, sizeof(app->act.state), "off");
    app->act.scan_ms = app->opt.activity_scan_ms > 0 ? app->opt.activity_scan_ms : DEFAULT_SCAN_MS;
    app->act.heartbeat_ms = app->opt.activity_heartbeat_ms > 0 ? app->opt.activity_heartbeat_ms : DEFAULT_HEARTBEAT_MS;
}

void app_activity_tick(akeno_app *app) {
    pthread_mutex_lock(&app->lock);
    int done = app->act_thread_running && app->act_thread_done;
    pthread_mutex_unlock(&app->lock);
    if (done) {
        pthread_join(app->act_thread, NULL);
        app->act_thread_running = 0;
    }
    pthread_mutex_lock(&app->lock);
    int paired = app->paired, enabled = app->activity_enabled, wanted = app->act_wanted;
    if ((!paired || !enabled) && app->act_thread_running) app->act_stop = 1;
    if (!app->act_thread_running) {
        const char *detail = !enabled ? "Turned off in the settings of this console." : !paired ? "Pair this console to share game activity." : NULL;
        if (detail) {
            app_copy_str(app->act.state, sizeof(app->act.state), "off");
            app_copy_str(app->act.detail, sizeof(app->act.detail), detail);
        }
    }
    int start = paired && enabled && wanted && !app->act_thread_running && !app->stop;
    if (start) {
        app->act_stop = 0;
        app->act_thread_done = 0;
        app_copy_str(app->act.state, sizeof(app->act.state), "starting");
        app_copy_str(app->act.detail, sizeof(app->act.detail), "Looking for a running game.");
    }
    pthread_mutex_unlock(&app->lock);
    if (!start) return;
    if (app_start_worker(&app->act_thread, act_main, app) == 0) {
        app->act_thread_running = 1;
    } else {
        app_log(app, AKENO_LOG_ERROR, "activity.thread_failed", "Could not start the game activity worker.");
        pthread_mutex_lock(&app->lock);
        app->act_wanted = 0;
        pthread_mutex_unlock(&app->lock);
    }
}

void app_activity_shutdown(akeno_app *app) {
    app->act_stop = 1;
    if (app->act_thread_running) {
        pthread_join(app->act_thread, NULL);
        app->act_thread_running = 0;
    }
}
