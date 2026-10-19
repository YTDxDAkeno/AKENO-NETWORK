#define _POSIX_C_SOURCE 200809L
#include "pv_engine.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "akeno/json.h"
#include "pv_internal.h"
#include "pv_status.h"

#define LOG_MAX_BYTES (512 * 1024)
#define REPORT_EVERY_US 10000000u
#define GAME_POLL_US 2000000u
#define PEOPLE_NOTE_US 4000000u
#define SHUTDOWN_WAIT_MS 15000

static const pv_route k_routes[PV_ROUTES] = {
    { 0, 0, 0, "system user, main port (AKENO Native's call)" },
    { 1, 0, 0, "foreground user, main port (refused on 12.20: 0x80260011)" },
    { 1, 2, 1, "retired: PS4 port type 2 (refused on 12.20 like route 2)" },
    { 1, 3, 1, "retired: PS4 port type 3 (refused on 12.20 like route 2)" },
};

const pv_route *pv_route_get(int route) { return route >= 1 && route <= PV_ROUTES ? &k_routes[route - 1] : NULL; }

long long pv_cpu_ms(void) {
#ifdef CLOCK_PROCESS_CPUTIME_ID
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
    return -1;
}

const char *pv_net_state_name(pv_net_state s) {
    switch (s) {
    case PV_NET_OFF: return "off";
    case PV_NET_REQUESTING: return "requesting";
    case PV_NET_CONNECTING: return "connecting";
    case PV_NET_CONNECTED: return "connected";
    case PV_NET_RECONNECTING: return "reconnecting";
    case PV_NET_ENDED: return "disconnected";
    }
    return "off";
}

const char *pv_mic_state_name(pv_mic_state s) {
    switch (s) {
    case PV_MIC_OFF: return "off";
    case PV_MIC_OPENING: return "opening";
    case PV_MIC_ACTIVE: return "active";
    case PV_MIC_UNAVAILABLE: return "unavailable";
    case PV_MIC_FAILED: return "failed";
    }
    return "off";
}

const char *pv_out_state_name(pv_out_state s) {
    switch (s) {
    case PV_OUT_OFF: return "off";
    case PV_OUT_OPENING: return "opening";
    case PV_OUT_ACTIVE: return "active";
    case PV_OUT_FAILED: return "failed";
    }
    return "off";
}

int pv_party_id_ok(const char *id) {
    size_t n = id ? strlen(id) : 0;
    if (n == 0 || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return 1;
}

uint64_t pv_now_ms(const pv_engine *e) { return e->be->now_us(e->be->user) / 1000u; }

void pv_sleep_ms(unsigned ms) {
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

void pv_sleep_while(const atomic_int *run, unsigned ms) {
    while (*run && ms > 0) {
        unsigned step = ms < 20 ? ms : 20;
        pv_sleep_ms(step);
        ms -= step;
    }
}

int pv_start_thread(pthread_t *thread, void *(*fn)(void *), void *arg) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return -1;
    int rc = pthread_attr_setstacksize(&attr, PV_THREAD_STACK);
    if (rc == 0) rc = pthread_create(thread, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc == 0 ? 0 : -1;
}

/* ---- Log and notifications ------------------------------------------------------------------- */

void pv_log(pv_engine *e, const char *event, const char *fmt, ...) {
    char text[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    double t = (double)(e->be->now_us(e->be->user) - e->started_us) / 1e6;
    char line[512];
    int n = snprintf(line, sizeof(line), "%10.3f %lld %s: %s\n", t, (long long)time(NULL), event, text);
    if (n < 0) return;
    if ((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    pthread_mutex_lock(&e->log_lock);
    printf("[akeno-party-voice] %s: %s\n", event, text);
    fflush(stdout);
    char path[300], old[310];
    snprintf(path, sizeof(path), "%s/voice.log", e->cfg.data_dir);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > LOG_MAX_BYTES) {
        snprintf(old, sizeof(old), "%s.1", path);
        rename(path, old);
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        if (write(fd, line, (size_t)n) < 0) {
            /* a full disk loses log lines, nothing else */
        }
        close(fd);
    }
    pthread_mutex_unlock(&e->log_lock);
}

void pv_note_locked(pv_engine *e, const char *fmt, ...) {
    if (!e->cfg.notifications || e->note_count >= PV_NOTES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->notes[e->note_count], sizeof(e->notes[0]), fmt, ap);
    va_end(ap);
    e->note_count++;
}

void pv_set_failure_locked(pv_engine *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->st.last_failure, sizeof(e->st.last_failure), fmt, ap);
    va_end(ap);
    e->report_dirty = 1;
}

int pv_transmitting_locked(const pv_engine *e) {
    return e->st.mic_enabled && e->st.mic == PV_MIC_ACTIVE && !e->st.muted && !e->st.deafened;
}

/* ---- Refusals: the same refused call is not repeated in the same situation -------------------- */

/* The situation is the device, the user and the running app (a game started or ended is a new
 * one, so each of the owner's tests - home screen, in a game - gets exactly one attempt). */
const pv_refusal *pv_refusal_find_locked(const pv_engine *e, int what, int32_t user) {
    for (int i = 0; i < e->refusal_count; i++) {
        const pv_refusal *r = &e->refusals[i];
        if (r->what == what && r->user == user && r->bigapp == e->st.bigapp) return r;
    }
    return NULL;
}

void pv_refusal_remember_locked(pv_engine *e, int what, int32_t user, int32_t rc) {
    pv_refusal *slot = NULL;
    for (int i = 0; i < e->refusal_count && !slot; i++)
        if (e->refusals[i].what == what && e->refusals[i].user == user) slot = &e->refusals[i];
    if (!slot && e->refusal_count < PV_REFUSALS) slot = &e->refusals[e->refusal_count++];
    if (!slot) { /* full: the oldest goes */
        memmove(&e->refusals[0], &e->refusals[1], sizeof(e->refusals[0]) * (PV_REFUSALS - 1));
        slot = &e->refusals[PV_REFUSALS - 1];
    }
    slot->what = what;
    slot->user = user;
    slot->bigapp = e->st.bigapp;
    slot->rc = rc;
}

void pv_refresh_process(pv_engine *e) {
    akn_context_raw raw;
    e->be->process(e->be->user, &raw);
    akn_context_kind kind = akn_context_classify(&raw);
    char detail[200];
    akn_context_describe(&raw, kind, detail, sizeof(detail));
    pthread_mutex_lock(&e->lock);
    int changed = kind != e->st.process_kind || strcmp(detail, e->st.process_detail) != 0;
    e->st.process_kind = kind;
    snprintf(e->st.process_detail, sizeof(e->st.process_detail), "%s", detail);
    e->report_dirty |= changed;
    pthread_mutex_unlock(&e->lock);
    if (changed) pv_log(e, "service.process", "this process: %s", detail);
}

/* ---- Settings (volume, gain, route; never the microphone permission) ------------------------- */

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void load_settings(pv_engine *e) {
    char path[300];
    snprintf(path, sizeof(path), "%s/settings.json", e->cfg.data_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    akeno_json_doc *doc = akeno_json_parse(buf, n, NULL, 0);
    const akeno_json *root = akeno_json_root(doc);
    int64_t v;
    if (akeno_json_int64(akeno_json_get(root, "outputVolume"), &v) == 0) e->st.out_volume = clamp((int)v, PV_VOLUME_MIN, PV_VOLUME_MAX);
    if (akeno_json_int64(akeno_json_get(root, "micGain"), &v) == 0) e->st.mic_gain = clamp((int)v, PV_GAIN_MIN, PV_GAIN_MAX);
    if (akeno_json_int64(akeno_json_get(root, "route"), &v) == 0 && pv_route_get((int)v)) e->st.route = (int)v;
    akeno_json_free(doc);
    if (pv_route_get(e->st.route)->retired) e->st.route = 1; /* chosen with 0.1.0; never opened again */
}

static void save_settings_locked(pv_engine *e) {
    char text[160];
    int n = snprintf(text, sizeof(text), "{\"outputVolume\":%d,\"micGain\":%d,\"route\":%d}\n", e->st.out_volume, e->st.mic_gain, e->st.route);
    char path[300];
    snprintf(path, sizeof(path), "%s/settings.json", e->cfg.data_dir);
    pv_write_file_atomic(path, text, (size_t)n);
}

/* ---- Lifecycle -------------------------------------------------------------------------------- */

pv_engine *pv_engine_new(const pv_config *config) {
    pv_engine *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->cfg = *config;
    e->be = config->be;
    pthread_mutex_init(&e->lock, NULL);
    pthread_mutex_init(&e->log_lock, NULL);
    e->started_us = e->last_tick_us = e->be->now_us(e->be->user);
    e->st.out_volume = 100;
    e->st.mic_gain = 100;
    e->st.route = 1;
    e->st.paced = -1;
    e->st.probe_verdict = -2;
    e->st.test_paced = -1;
    snprintf(e->st.backend, sizeof(e->st.backend), "%s", e->be->name);
    e->be->describe(e->be->user, e->st.platform, sizeof(e->st.platform));
    load_settings(e);
    e->report_dirty = 1;
    pv_refresh_process(e);
    return e;
}

static void session_free(pv_session *s) {
    if (s->have_net) pthread_join(s->net, NULL);
    if (s->have_play) pthread_join(s->play, NULL);
    if (s->have_cap) pthread_join(s->cap, NULL);
    pthread_mutex_destroy(&s->lock);
    free(s);
}

static int session_finished(const pv_session *s) { return s->net_done && s->play_done && s->cap_done; }

/* Moves the current session to the closing list (e->lock held). */
static void retire_locked(pv_engine *e) {
    pv_session *s = e->session;
    if (!s) return;
    s->run = 0;
    s->cap_run = 0;
    e->session = NULL;
    if (e->closing_count < (int)(sizeof(e->closing) / sizeof(e->closing[0]))) {
        e->closing[e->closing_count++] = s;
    } else {
        /* never expected (at most one per join); wait for the oldest here */
        pv_session *old = e->closing[0];
        for (int i = 1; i < e->closing_count; i++) e->closing[i - 1] = e->closing[i];
        e->closing[e->closing_count - 1] = s;
        pthread_mutex_unlock(&e->lock);
        session_free(old);
        pthread_mutex_lock(&e->lock);
    }
}

void pv_engine_free(pv_engine *e) {
    if (!e) return;
    pv_engine_leave(e, "The voice service stopped.");
    uint64_t end = pv_now_ms(e) + SHUTDOWN_WAIT_MS;
    for (;;) {
        pthread_mutex_lock(&e->lock);
        int busy = e->have_test && !e->test_done;
        for (int i = 0; i < e->closing_count; i++) busy |= !session_finished(e->closing[i]);
        pthread_mutex_unlock(&e->lock);
        if (!busy) break;
        if (pv_now_ms(e) > end) {
            /* a thread is stuck in a system call: exit without joining it (the process ends) */
            pv_log(e, "service.exit", "a voice thread did not finish in %d s; exiting anyway", SHUTDOWN_WAIT_MS / 1000);
            pv_engine_write_report(e);
            return;
        }
        pv_sleep_ms(20);
    }
    for (int i = 0; i < e->closing_count; i++) session_free(e->closing[i]);
    e->closing_count = 0;
    if (e->have_test) pthread_join(e->test_thread, NULL);
    pv_engine_write_report(e); /* the final report: every port is closed now */
    pthread_mutex_destroy(&e->lock);
    pthread_mutex_destroy(&e->log_lock);
    free(e);
}

/* ---- Controls --------------------------------------------------------------------------------- */

/* The session the controls act on. One whose network thread ended by itself (removed, refused, gave up)
 * is already gone, even before the next tick retires it. */
static pv_session *live_session_locked(pv_engine *e) {
    return e->session && e->st.net != PV_NET_ENDED ? e->session : NULL;
}

static int fail(pv_error *err, const char *code, const char *message) {
    if (err) {
        snprintf(err->code, sizeof(err->code), "%s", code);
        snprintf(err->message, sizeof(err->message), "%s", message);
    }
    return -1;
}

static void reset_session_stats_locked(pv_engine *e) {
    pv_status *s = &e->st;
    s->net_error[0] = '\0';
    s->close_code = 0;
    s->attempts = 0;
    s->ready_seen = s->participants_seen = 0;
    s->latency_ms = 0;
    s->connected_since_ms = 0;
    s->people_count = 0;
    s->mic_enabled = 0;
    s->muted = 0;
    s->deafened = 0;
    s->mic = PV_MIC_OFF;
    s->mic_rc = 0;
    s->mic_refusal = PV_REFUSAL_NONE;
    s->mic_detail[0] = '\0';
    s->mic_level = 0;
    s->out = PV_OUT_OFF;
    s->rx_level = 0;
    e->known_count = 0;
    e->people_noted = 0;
}

int pv_engine_join(pv_engine *e, const char *party_id, pv_error *err) {
    if (!pv_party_id_ok(party_id)) return fail(err, "INVALID_PARTY", "That is not a party ID.");
    pv_refresh_process(e);
    pthread_mutex_lock(&e->lock);
    if (live_session_locked(e) && strcmp(e->session->party_id, party_id) == 0) {
        pthread_mutex_unlock(&e->lock);
        return 0; /* already in this party's voice */
    }
    for (int i = 0; i < e->closing_count; i++) {
        if (!session_finished(e->closing[i]) && e->closing_count >= 3) {
            pthread_mutex_unlock(&e->lock);
            return fail(err, "BUSY", "Voice is still closing. Try again in a moment.");
        }
    }
    if (e->session) retire_locked(e); /* switching parties, or one that ended: leave it */
    pv_session *s = calloc(1, sizeof(*s));
    if (!s) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "NO_MEMORY", "Out of memory.");
    }
    s->e = e;
    s->run = 1;
    snprintf(s->party_id, sizeof(s->party_id), "%s", party_id);
    pthread_mutex_init(&s->lock, NULL);
    akn_jitter_init(&s->jitter);
    reset_session_stats_locked(e);
    e->st.in_voice = 1;
    snprintf(e->st.party_id, sizeof(e->st.party_id), "%s", party_id);
    e->st.net = PV_NET_REQUESTING;
    e->st.sessions++;
    e->session = s;
    e->report_dirty = 1;
    /* the threads read the engine under its lock; start them after it is released */
    pthread_mutex_unlock(&e->lock);
    s->cap_done = 1; /* no microphone until the user turns it on */
    if (pv_start_thread(&s->play, pv_play_thread, s) == 0) s->have_play = 1;
    else s->play_done = 1;
    if (pv_start_thread(&s->net, pv_net_thread, s) == 0) s->have_net = 1;
    else s->net_done = 1;
    pv_log(e, "voice.join", "joining party voice (microphone off until the user turns it on)");
    if (!s->have_net) {
        pv_engine_leave(e, "The voice service could not start its network thread.");
        return fail(err, "NO_THREAD", "The voice service could not start.");
    }
    return 0;
}

void pv_engine_leave(pv_engine *e, const char *why) {
    pthread_mutex_lock(&e->lock);
    int was = e->session != NULL;
    retire_locked(e);
    e->st.in_voice = 0;
    e->st.mic_enabled = 0;
    if (was || e->st.net == PV_NET_ENDED) {
        e->st.net = PV_NET_OFF;
        snprintf(e->st.net_error, sizeof(e->st.net_error), "%s", why ? why : "");
    }
    e->st.people_count = 0;
    if (e->st.connected_since_ms) e->st.connected_total_ms += pv_now_ms(e) - e->st.connected_since_ms;
    e->st.connected_since_ms = 0;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (was) pv_log(e, "voice.leave", "%s", why ? why : "left");
}

/* The answer when a "turn on" meets a refusal already seen in this situation. */
static int mic_refused(pv_engine *e, pv_error *err, int32_t rc, pv_refusal_kind kind, const char *how) {
    e->st.mic = kind == PV_REFUSAL_NO_LIBRARY ? PV_MIC_UNAVAILABLE : PV_MIC_FAILED;
    e->st.mic_rc = rc;
    e->st.mic_refusal = kind;
    e->st.mic_enabled = 0;
    snprintf(e->st.mic_detail, sizeof(e->st.mic_detail), "%s", how);
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.mic", "turn on: %s (0x%08X, %s)", how, (unsigned)rc, pv_refusal_name(kind));
    char message[200];
    if (kind == PV_REFUSAL_PROCESS)
        snprintf(message, sizeof(message), "The PS5 refused the microphone to this background service (0x%08X). It is tried again when a game starts or ends.",
                 (unsigned)rc);
    else if (kind == PV_REFUSAL_NO_USER)
        snprintf(message, sizeof(message), "No signed-in user in front (0x%08X): the microphone needs one.", (unsigned)rc);
    else
        snprintf(message, sizeof(message), "The microphone was refused (0x%08X, %s). It is tried again when a game starts or ends.", (unsigned)rc,
                 pv_refusal_name(kind));
    return fail(err, kind == PV_REFUSAL_NO_USER ? "NO_USER" : "MIC_REFUSED", message);
}

int pv_engine_set_mic(pv_engine *e, int enabled, pv_error *err) {
    int32_t user = 0, user_rc = enabled ? e->be->foreground_user(e->be->user, &user) : 0;
    pthread_mutex_lock(&e->lock);
    pv_session *s = live_session_locked(e);
    if (!s) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "NOT_IN_VOICE", "Join the voice chat first.");
    }
    if (!enabled) {
        int was = e->st.mic_enabled;
        e->st.mic_enabled = 0;
        s->cap_run = 0;
        e->report_dirty = 1;
        pthread_mutex_unlock(&e->lock);
        if (was) pv_log(e, "voice.mic", "the user turned the microphone off");
        return 0;
    }
    if (e->st.mic_enabled && s->have_cap && !s->cap_done) {
        e->st.muted = 0;
        pthread_mutex_unlock(&e->lock);
        return 0;
    }
    if (s->have_cap && !s->cap_done) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "BUSY", "The microphone is still closing. Try again in a moment.");
    }
    if (user_rc != 0) return mic_refused(e, err, user_rc, PV_REFUSAL_NO_USER, "the foreground user is unknown; nothing was opened");
    const pv_refusal *known = pv_refusal_find_locked(e, 0, user);
    if (known) {
        e->st.mic_avoided++;
        return mic_refused(e, err, known->rc, pv_refusal_of(known->rc, 1),
                           "not tried again: refused before in this situation (same user, same running app)");
    }
    if (e->st.mic_open_calls >= PV_MIC_OPEN_LIMIT) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "MIC_LIMIT", "The microphone was tried 8 times in this run. Restart the voice service to try again.");
    }
    if (s->have_cap) {
        pthread_join(s->cap, NULL); /* finished: cap_done is set */
        s->have_cap = 0;
    }
    s->cap_user = user;
    e->st.mic_enabled = 1;
    e->st.muted = 0;
    e->st.deafened = 0;
    e->st.mic = PV_MIC_OPENING;
    s->cap_run = 1;
    s->cap_done = 0;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (pv_start_thread(&s->cap, pv_cap_thread, s) != 0) {
        pthread_mutex_lock(&e->lock);
        s->cap_done = 1;
        s->cap_run = 0;
        e->st.mic = PV_MIC_FAILED;
        e->st.mic_enabled = 0;
        pthread_mutex_unlock(&e->lock);
        return fail(err, "NO_THREAD", "The microphone thread could not start.");
    }
    s->have_cap = 1;
    pv_log(e, "voice.mic", "the user turned the microphone on (consent for this voice session)");
    return 0;
}

int pv_engine_set_mute(pv_engine *e, int muted, pv_error *err) {
    pthread_mutex_lock(&e->lock);
    if (!live_session_locked(e)) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "NOT_IN_VOICE", "Join the voice chat first.");
    }
    if (!muted && !e->st.mic_enabled) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "MIC_OFF", "Turn the microphone on first.");
    }
    e->st.muted = muted ? 1 : 0;
    if (!muted) e->st.deafened = 0; /* unmuting also undeafens */
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    return 0;
}

void pv_engine_set_deafen(pv_engine *e, int deafened) {
    pthread_mutex_lock(&e->lock);
    e->st.deafened = deafened ? 1 : 0;
    if (deafened) e->st.muted = 1; /* deafened also means not speaking */
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
}

int pv_engine_set_volume(pv_engine *e, int percent) {
    pthread_mutex_lock(&e->lock);
    e->st.out_volume = clamp(percent, PV_VOLUME_MIN, PV_VOLUME_MAX);
    int v = e->st.out_volume;
    save_settings_locked(e);
    pthread_mutex_unlock(&e->lock);
    return v;
}

int pv_engine_set_gain(pv_engine *e, int percent) {
    pthread_mutex_lock(&e->lock);
    e->st.mic_gain = clamp(percent, PV_GAIN_MIN, PV_GAIN_MAX);
    int v = e->st.mic_gain;
    save_settings_locked(e);
    pthread_mutex_unlock(&e->lock);
    return v;
}

int pv_engine_set_route(pv_engine *e, int route, pv_error *err) {
    if (!pv_route_get(route)) return fail(err, "INVALID_ROUTE", "Choose a speaker route from 1 to 4.");
    if (pv_route_get(route)->retired) {
        char message[200];
        snprintf(message, sizeof(message), "Route %d is retired: the PS5 refused it on FW 12.20 like route 2 (0x80260011). Use route 1.", route);
        return fail(err, "ROUTE_RETIRED", message);
    }
    pthread_mutex_lock(&e->lock);
    e->st.route = route;
    save_settings_locked(e);
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.output", "speaker route %d chosen (%s); it applies to the next output port", route, pv_route_get(route)->label);
    return 0;
}

int pv_engine_test_sound(pv_engine *e, pv_error *err) {
    pthread_mutex_lock(&e->lock);
    if (e->test_pending || (e->have_test && !e->test_done)) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "BUSY", "A test sound is playing.");
    }
    if (e->st.out_volume == 0) {
        pthread_mutex_unlock(&e->lock);
        return fail(err, "VOLUME_ZERO", "The voice volume is 0 %: turn it up to hear the test sound.");
    }
    int route = e->st.route;
    if (e->session && e->session->have_play && !e->session->play_done) {
        if (e->st.out == PV_OUT_FAILED) {
            char message[200];
            snprintf(message, sizeof(message), "Speaker route %d is not open (0x%08X): there is no output to play the test sound on.", route,
                     (unsigned)e->st.out_rc);
            pthread_mutex_unlock(&e->lock);
            return fail(err, "OUTPUT_UNAVAILABLE", message);
        }
        e->st.test_sounds++;
        e->test_pending = route; /* the playback thread mixes it into the voice output */
        e->st.test_state = PV_TEST_PLAYING;
        pthread_mutex_unlock(&e->lock);
        pv_log(e, "voice.test_sound", "test sound requested on route %d: %d beep%s through the voice pipeline, mixed into the voice output", route,
               route, route == 1 ? "" : "s");
        return 0;
    }
    e->st.test_sounds++;
    if (e->have_test) {
        pthread_join(e->test_thread, NULL);
        e->have_test = 0;
    }
    e->test_done = 0;
    e->test_pending = route;
    e->st.test_state = PV_TEST_PLAYING;
    pthread_mutex_unlock(&e->lock);
    pv_refresh_process(e);
    pv_log(e, "voice.test_sound", "test sound requested on route %d outside voice: %d beep%s through the voice pipeline on a port of its own", route,
           route, route == 1 ? "" : "s");
    if (pv_start_thread(&e->test_thread, pv_test_thread, e) != 0) {
        pthread_mutex_lock(&e->lock);
        e->test_pending = 0;
        pthread_mutex_unlock(&e->lock);
        return fail(err, "NO_THREAD", "The test sound could not start.");
    }
    e->have_test = 1;
    return 0;
}

void pv_engine_status(pv_engine *e, pv_status *out) {
    pthread_mutex_lock(&e->lock);
    *out = e->st;
    uint64_t now = pv_now_ms(e);
    out->uptime_ms = now - e->started_us / 1000u;
    if (out->connected_since_ms) out->connected_total_ms += now - out->connected_since_ms;
    pthread_mutex_unlock(&e->lock);
}

/* ---- Main loop -------------------------------------------------------------------------------- */

void pv_engine_write_report(pv_engine *e) {
    static char text[24 * 1024];
    pv_status s;
    pv_engine_status(e, &s);
    size_t n = pv_report_text(&s, text, sizeof(text));
    char path[300];
    snprintf(path, sizeof(path), "%s/report.txt", e->cfg.data_dir);
    if (pv_write_file_atomic(path, text, n) != 0) pv_log(e, "report", "the report could not be written (errno %d)", errno);
}

/* Participant join/leave notifications, at most one every few seconds (e->lock held). The first
 * list of a session is only remembered: the people already there are not news. */
static void people_notes_locked(pv_engine *e, uint64_t now_us) {
    if (e->st.net != PV_NET_CONNECTED || !e->st.participants_seen) return;
    int joins = 0, leaves = 0;
    const char *first = "";
    for (int i = 0; i < e->st.people_count; i++) {
        const akn_voice_participant *p = &e->st.people[i];
        if (p->self) continue;
        int known = 0;
        for (int k = 0; k < e->known_count; k++) known |= strcmp(e->known_people[k], p->id) == 0;
        if (!known && !joins++) first = p->name[0] ? p->name : "Someone";
    }
    for (int k = 0; k < e->known_count; k++) {
        int still = 0;
        for (int i = 0; i < e->st.people_count; i++) still |= !e->st.people[i].self && strcmp(e->st.people[i].id, e->known_people[k]) == 0;
        leaves += !still;
    }
    if (e->people_noted && !joins && !leaves) return;
    if (e->people_noted && now_us - e->last_people_note_us < PEOPLE_NOTE_US) return; /* later, as one note */
    if (e->people_noted) {
        if (joins == 1 && !leaves) pv_note_locked(e, "AKENO Voice: %s joined the voice chat", first);
        else if (joins > 1 && !leaves) pv_note_locked(e, "AKENO Voice: %s and %d more joined the voice chat", first, joins - 1);
        else if (!joins) pv_note_locked(e, "AKENO Voice: %d left the voice chat", leaves);
        else pv_note_locked(e, "AKENO Voice: %d joined, %d left the voice chat", joins, leaves);
    }
    e->known_count = 0;
    for (int i = 0; i < e->st.people_count && e->known_count < AKN_VOICE_MAX_PARTICIPANTS; i++)
        if (!e->st.people[i].self) snprintf(e->known_people[e->known_count++], sizeof(e->known_people[0]), "%s", e->st.people[i].id);
    e->people_noted = 1;
    e->last_people_note_us = now_us;
}

void pv_engine_tick(pv_engine *e) {
    uint64_t now = e->be->now_us(e->be->user);
    pv_session *done[4];
    int done_n = 0;
    char notes[PV_NOTES][200];
    int notes_n = 0;
    int join_test = 0, refresh = 0;

    pthread_mutex_lock(&e->lock);
    int gap = (int)((now - e->last_tick_us) / 1000u);
    if (gap > e->st.heartbeat_gap_max_ms) e->st.heartbeat_gap_max_ms = gap;
    e->last_tick_us = now;
    e->st.heartbeats++;
    /* a session that ended by itself (removed, refused, gave up reconnecting) */
    if (e->session && e->session->net_done) {
        e->st.in_voice = 0;
        e->st.mic_enabled = 0;
        e->st.people_count = 0;
        retire_locked(e);
        e->report_dirty = 1;
    }
    for (int i = 0; i < e->closing_count;) {
        if (session_finished(e->closing[i])) {
            done[done_n++] = e->closing[i];
            e->closing[i] = e->closing[--e->closing_count];
        } else {
            i++;
        }
    }
    if (e->have_test && e->test_done) join_test = 1;
    /* game activity: a read-only query of the running app every 2 s */
    if (now - e->last_game_poll_us >= GAME_POLL_US) {
        uint64_t elapsed = e->last_game_poll_us ? now - e->last_game_poll_us : 0;
        e->last_game_poll_us = now;
        pthread_mutex_unlock(&e->lock);
        int32_t app = e->be->bigapp(e->be->user);
        pthread_mutex_lock(&e->lock);
        if (app != e->st.bigapp) {
            if (e->st.heartbeats > 1) e->st.game_changes++;
            e->st.bigapp = app;
            e->report_dirty = 1;
            refresh = 1;
        }
        e->st.game_running = app > 0;
        if (e->st.game_running && e->st.net == PV_NET_CONNECTED) e->st.game_seconds_in_voice += (unsigned long)(elapsed / 1000000u);
    }
    e->st.cpu_ms = pv_cpu_ms();
    people_notes_locked(e, now);
    for (int i = 0; i < e->note_count; i++) snprintf(notes[notes_n++], sizeof(notes[0]), "%s", e->notes[i]);
    e->note_count = 0;
    int report = e->report_dirty || (e->st.in_voice && now - e->last_report_us >= REPORT_EVERY_US);
    if (report) {
        e->report_dirty = 0;
        e->last_report_us = now;
    }
    pthread_mutex_unlock(&e->lock);

    for (int i = 0; i < done_n; i++) session_free(done[i]);
    if (refresh) pv_refresh_process(e);
    if (join_test) {
        pthread_join(e->test_thread, NULL);
        pthread_mutex_lock(&e->lock);
        e->have_test = 0;
        pthread_mutex_unlock(&e->lock);
    }
    for (int i = 0; i < notes_n; i++) e->be->notify(e->be->user, notes[i]);
    if (report) pv_engine_write_report(e);
}
