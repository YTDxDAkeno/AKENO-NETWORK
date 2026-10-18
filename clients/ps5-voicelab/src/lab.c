#include "lab.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE 48000u
#define OUT_GRAIN 1024u
#define MIC_RATE 16000u
#define MIC_GRAIN 256u
#define ECHO_GRAIN 768u            /* 256 microphone samples, three times over, at 48 kHz */
#define USER_SYSTEM 0xFF

static const akn_lab_tone_variant k_variants[AKN_LAB_TONES] = {
    { "system user, main port (as audio labs v1-v3 and AKENO Native)", 0, 0 },
    { "foreground user, main port", 1, 0 },
    { "foreground user, voice port (type 2, PS4 numbering, unverified)", 1, 2 },
    { "foreground user, personal port (type 3, PS4 numbering, unverified)", 1, 3 },
};

const akn_lab_tone_variant *akn_lab_tone_variants(void) { return k_variants; }

static void logf_(akn_lab_run *run, const akn_lab_backend *be, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void logf_(akn_lab_run *run, const akn_lab_backend *be, const char *fmt, ...) {
    double t = (double)(be->now_us(be->user) - run->started_us) / 1e6;
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (run->log_len + 1 >= sizeof(run->log)) return;
    int n = snprintf(run->log + run->log_len, sizeof(run->log) - run->log_len, "%8.1f s  %s\n", t, line);
    if (n > 0) run->log_len += (size_t)n < sizeof(run->log) - run->log_len ? (size_t)n : sizeof(run->log) - run->log_len - 1;
}

static void notify(akn_lab_run *run, const akn_lab_backend *be, const char *text) {
    int rc = be->notify(be->user, text);
    logf_(run, be, "notification%s: %s", rc == 0 ? "" : " (failed)", text);
}

static void wait_s(const akn_lab_backend *be, const akn_lab_options *o, unsigned s) {
    be->sleep_ms(be->user, o->quick ? (s ? 20 : 0) : s * 1000u);
}

/* ---- D: heartbeat and network monitor ---------------------------------------------------------- */

typedef struct {
    const akn_lab_backend *be;
    akn_lab_run *run;
    pthread_mutex_t lock;
    volatile int stop;
    volatile int phase;
} monitor;

static void *monitor_main(void *arg) {
    monitor *m = arg;
    const akn_lab_backend *be = m->be;
    uint64_t last = be->now_us(be->user), last_probe = 0;
    while (!m->stop) {
        be->sleep_ms(be->user, m->run->opt.quick ? 5 : 250);
        uint64_t now = be->now_us(be->user);
        int p = m->phase;
        pthread_mutex_lock(&m->lock);
        akn_lab_net_result *n = &m->run->net[p];
        double gap = (double)(now - last) / 1000.0;
        if (gap > n->gap_max_ms) n->gap_max_ms = gap;
        n->beats++;
        pthread_mutex_unlock(&m->lock);
        last = now;
        if (now - last_probe >= (m->run->opt.quick ? 20000u : 10000000u)) {
            last_probe = now;
            int ms = 0, sms = 0;
            int d = be->probe_daemon(be->user, &ms);
            char host[128] = "";
            int s = be->probe_server(be->user, &sms, host, sizeof(host));
            pthread_mutex_lock(&m->lock);
            if (d == 0) n->daemon_ok++;
            else n->daemon_fail++;
            if (ms > n->daemon_ms_max) n->daemon_ms_max = ms;
            if (s == 0) n->server_ok++;
            else if (s > 0) n->server_fail++; /* < 0: no server configured */
            if (sms > n->server_ms_max) n->server_ms_max = sms;
            if (host[0]) snprintf(m->run->server_host, sizeof(m->run->server_host), "%s", host);
            pthread_mutex_unlock(&m->lock);
        }
    }
    return NULL;
}

/* ---- A: microphone ---------------------------------------------------------------------------- */

static void mic_capture(const akn_lab_backend *be, akn_lab_run *run, unsigned seconds, akn_lab_mic_result *r) {
    memset(r, 0, sizeof(*r));
    akn_lab_mic_stats_init(&r->stats, MIC_RATE);
    int32_t user = 0;
    r->user_rc = be->foreground_user(be->user, &user);
    if (r->user_rc != 0) {
        snprintf(r->why, sizeof(r->why), "no foreground user (0x%08x)", (unsigned)r->user_rc);
        r->open_rc = r->user_rc;
        r->verdict = AKN_LAB_PCM_NONE;
        return;
    }
    int32_t h = be->mic_open(be->user, user, r->why, sizeof(r->why));
    r->open_rc = h < 0 ? h : 0;
    logf_(run, be, "microphone open: %s (0x%08x) %s", h >= 0 ? "ok" : "FAILED", (unsigned)(h < 0 ? h : 0), r->why);
    if (h < 0) {
        r->verdict = AKN_LAB_PCM_NONE;
        return;
    }
    int16_t buf[MIC_GRAIN];
    unsigned long want = (unsigned long)seconds * MIC_RATE / MIC_GRAIN;
    if (run->opt.quick) want = want < 20 ? want : 20;
    uint64_t t0 = be->now_us(be->user);
    for (unsigned long i = 0; i < want; i++) {
        int32_t rc = be->mic_read(be->user, h, buf);
        r->reads++;
        if (rc < 0) {
            if (!r->read_errors) r->first_error = rc;
            r->read_errors++;
            if (r->read_errors > 20) break;
            continue;
        }
        akn_lab_mic_stats_add(&r->stats, buf, MIC_GRAIN);
    }
    r->read_ms_avg = r->reads ? (double)(be->now_us(be->user) - t0) / 1000.0 / (double)r->reads : 0;
    r->close_rc = be->mic_close(be->user, h);
    r->verdict = akn_lab_mic_verdict(&r->stats);
    logf_(run, be, "microphone: %lu reads, %lu errors, %s, %.1f dBFS, peak %d, active windows %lu/%lu",
          r->reads, r->read_errors, akn_lab_pcm_verdict_name(r->verdict), akn_lab_mic_rms_db(&r->stats), r->stats.peak,
          r->stats.windows_active, r->stats.windows);
}

/* ---- B: output -------------------------------------------------------------------------------- */

static void tone(const akn_lab_backend *be, akn_lab_run *run, const akn_lab_tone_variant *v, unsigned beeps,
                 akn_lab_tone_result *r) {
    memset(r, 0, sizeof(*r));
    r->beeps = beeps;
    int32_t user = USER_SYSTEM;
    if (v->foreground_user) {
        int32_t u = 0;
        int32_t rc = be->foreground_user(be->user, &u);
        if (rc != 0) {
            r->open_rc = rc;
            logf_(run, be, "tone %u: no foreground user (0x%08x)", beeps, (unsigned)rc);
            return;
        }
        user = u;
    }
    int32_t h = be->out_open(be->user, user, v->port_type, OUT_GRAIN);
    r->open_rc = h < 0 ? h : 0;
    if (h < 0) {
        logf_(run, be, "tone %u (%s): open FAILED 0x%08x", beeps, v->label, (unsigned)h);
        return;
    }
    akn_lab_beeps b;
    akn_lab_beeps_init(&b, OUT_RATE, beeps, 0.20); /* -14 dBFS peak, the audio labs' "clearer" level */
    static int16_t block[OUT_GRAIN * 2];
    double total_ms = 0;
    while (!akn_lab_beeps_done(&b)) {
        akn_lab_beeps_fill(&b, block, OUT_GRAIN);
        uint64_t t = be->now_us(be->user);
        int32_t rc = be->out_output(be->user, h, block);
        double ms = (double)(be->now_us(be->user) - t) / 1000.0;
        r->calls++;
        if (r->calls > 1) { /* the first call fills the queue and returns at once */
            total_ms += ms;
            if (ms > r->max_ms) r->max_ms = ms;
        }
        if (rc < 0) {
            if (!r->first_error_call) {
                r->first_error_call = r->calls;
                r->first_error = rc;
            }
        } else {
            r->ok++;
        }
    }
    be->out_output(be->user, h, NULL); /* drain */
    r->avg_ms = r->calls > 1 ? total_ms / (double)(r->calls - 1) : 0;
    double block_ms = 1000.0 * OUT_GRAIN / OUT_RATE;
    r->paced = r->calls > 2 && r->avg_ms > block_ms * 0.6 && r->avg_ms < block_ms * 1.6;
    r->close_rc = be->out_close(be->user, h);
    logf_(run, be, "tone %u (%s): %lu/%lu calls ok, %.1f ms per call (%s)", beeps, v->label, r->ok, r->calls, r->avg_ms,
          r->paced ? "paced" : "NOT paced");
}

static void tones(const akn_lab_backend *be, akn_lab_run *run, akn_lab_phase phase) {
    for (int i = 0; i < AKN_LAB_TONES; i++) {
        char text[200];
        snprintf(text, sizeof(text), "AKENO Voice Lab: test tone %d of %d now - count the beeps (%d)", i + 1, AKN_LAB_TONES,
                 i + 1);
        notify(run, be, text);
        wait_s(be, &run->opt, 3);
        tone(be, run, &k_variants[i], (unsigned)(i + 1), &run->tones[phase][i]);
        wait_s(be, &run->opt, 4);
    }
}

/* ---- C: echo (full duplex) -------------------------------------------------------------------- */

typedef struct {
    const akn_lab_backend *be;
    akn_lab_run *run;
    int32_t mic;
    akn_lab_ring ring;
    pthread_mutex_t lock;
    volatile int stop;
    unsigned long reads;
} echo_ctx;

static void *echo_capture(void *arg) {
    echo_ctx *e = arg;
    akn_lab_mic_result *r = &e->run->echo.mic;
    int16_t buf[MIC_GRAIN];
    uint64_t t0 = e->be->now_us(e->be->user);
    while (!e->stop) {
        int32_t rc = e->be->mic_read(e->be->user, e->mic, buf);
        r->reads++;
        if (rc < 0) {
            if (!r->read_errors) r->first_error = rc;
            if (++r->read_errors > 20) break;
            continue;
        }
        akn_lab_mic_stats_add(&r->stats, buf, MIC_GRAIN);
        pthread_mutex_lock(&e->lock);
        akn_lab_ring_push(&e->ring, buf, MIC_GRAIN);
        pthread_mutex_unlock(&e->lock);
    }
    r->read_ms_avg = r->reads ? (double)(e->be->now_us(e->be->user) - t0) / 1000.0 / (double)r->reads : 0;
    return NULL;
}

static void echo(const akn_lab_backend *be, akn_lab_run *run, unsigned seconds) {
    akn_lab_echo_result *r = &run->echo;
    memset(r, 0, sizeof(*r));
    akn_lab_mic_stats_init(&r->mic.stats, MIC_RATE);
    int32_t user = 0;
    r->mic.user_rc = be->foreground_user(be->user, &user);
    if (r->mic.user_rc != 0) {
        r->mic.open_rc = r->mic.user_rc;
        return;
    }
    /* The output variant: the foreground user's main port if it opens, else the system user's. */
    int32_t out = be->out_open(be->user, user, 0, ECHO_GRAIN);
    if (out < 0) out = be->out_open(be->user, USER_SYSTEM, 0, ECHO_GRAIN);
    r->out_open_rc = out < 0 ? out : 0;
    int32_t mic = be->mic_open(be->user, user, r->mic.why, sizeof(r->mic.why));
    r->mic.open_rc = mic < 0 ? mic : 0;
    if (out < 0 || mic < 0) {
        logf_(run, be, "echo: output 0x%08x, microphone 0x%08x: not run", (unsigned)r->out_open_rc, (unsigned)r->mic.open_rc);
        if (out >= 0) be->out_close(be->user, out);
        if (mic >= 0) be->mic_close(be->user, mic);
        return;
    }
    echo_ctx e;
    memset(&e, 0, sizeof(e));
    e.be = be;
    e.run = run;
    e.mic = mic;
    pthread_mutex_init(&e.lock, NULL);
    if (akn_lab_ring_init(&e.ring, MIC_RATE) != 0) { /* one second */
        be->out_close(be->user, out);
        be->mic_close(be->user, mic);
        return;
    }
    pthread_t cap;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    int started = pthread_create(&cap, &attr, echo_capture, &e) == 0;
    pthread_attr_destroy(&attr);
    /* 400 ms delay so the player can tell their own voice from the room. */
    be->sleep_ms(be->user, run->opt.quick ? 5 : 400);
    int16_t mono[MIC_GRAIN];
    static int16_t stereo[ECHO_GRAIN * 2];
    unsigned long blocks = (unsigned long)seconds * OUT_RATE / ECHO_GRAIN;
    if (run->opt.quick) blocks = blocks < 20 ? blocks : 20;
    double total_ms = 0;
    for (unsigned long i = 0; started && i < blocks; i++) {
        pthread_mutex_lock(&e.lock);
        akn_lab_ring_pop(&e.ring, mono, MIC_GRAIN);
        pthread_mutex_unlock(&e.lock);
        akn_lab_upsample3_stereo(mono, MIC_GRAIN, stereo, 0.8);
        uint64_t t = be->now_us(be->user);
        int32_t rc = be->out_output(be->user, out, stereo);
        if (i) total_ms += (double)(be->now_us(be->user) - t) / 1000.0;
        r->out_calls++;
        if (rc >= 0) r->out_ok++;
    }
    be->out_output(be->user, out, NULL);
    e.stop = 1;
    if (started) pthread_join(cap, NULL);
    r->out_avg_ms = r->out_calls > 1 ? total_ms / (double)(r->out_calls - 1) : 0;
    r->underruns = e.ring.underrun;
    r->dropped = e.ring.dropped;
    r->mic.close_rc = be->mic_close(be->user, mic);
    be->out_close(be->user, out);
    r->mic.verdict = akn_lab_mic_verdict(&r->mic.stats);
    akn_lab_ring_free(&e.ring);
    pthread_mutex_destroy(&e.lock);
    logf_(run, be, "echo: %lu/%lu output calls ok (%.1f ms each), microphone %s, %llu samples missing", r->out_ok, r->out_calls,
          r->out_avg_ms, akn_lab_pcm_verdict_name(r->mic.verdict), r->underruns);
}

/* ---- the plan --------------------------------------------------------------------------------- */

static void phase_tests(const akn_lab_backend *be, akn_lab_run *run, akn_lab_phase phase) {
    run->ran[phase] = 1;
    notify(run, be, phase == AKN_PHASE_GAME ? "AKENO Voice Lab: microphone test - speak normally for 8 seconds now"
                                            : "AKENO Voice Lab: microphone test - speak normally for 6 seconds now");
    wait_s(be, &run->opt, 2);
    mic_capture(be, run, phase == AKN_PHASE_GAME ? 8 : 6, &run->mic[phase]);
    wait_s(be, &run->opt, 2);
    tones(be, run, phase);
}

void akn_lab_execute(const akn_lab_backend *be, const akn_lab_options *opt, akn_lab_run *run) {
    memset(run, 0, sizeof(*run));
    run->opt = *opt;
    if (!run->opt.wait_game_s) run->opt.wait_game_s = 300;
    if (!run->opt.settle_s) run->opt.settle_s = 30;
    run->started_us = be->now_us(be->user);
    be->describe(be->user, run->context, sizeof(run->context));
    run->bigapp_start = be->bigapp_id(be->user);
    run->out_init_rc = be->out_init(be->user);
    logf_(run, be, "AKENO Voice Lab %s on %s; running app id 0x%x; audio init 0x%08x", AKN_LAB_VERSION, be->name,
          (unsigned)run->bigapp_start, (unsigned)run->out_init_rc);

    monitor m;
    memset(&m, 0, sizeof(m));
    m.be = be;
    m.run = run;
    pthread_mutex_init(&m.lock, NULL);
    pthread_t mt;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    int monitoring = pthread_create(&mt, &attr, monitor_main, &m) == 0;
    pthread_attr_destroy(&attr);

    if (!run->opt.skip_home) {
        notify(run, be, "AKENO Voice Lab started. Stay on the home screen; the first tests begin in 10 seconds.");
        wait_s(be, &run->opt, 10);
        phase_tests(be, run, AKN_PHASE_HOME);
        notify(run, be, "AKENO Voice Lab: now start a game and play. The game tests begin about 30 seconds after it runs.");
    }
    m.phase = AKN_PHASE_GAME;
    /* Wait for a game: the running app changes to a new app id. */
    uint64_t deadline = be->now_us(be->user) + (uint64_t)run->opt.wait_game_s * 1000000u;
    for (;;) {
        int32_t id = be->bigapp_id(be->user);
        if (id > 0 && (run->opt.skip_home || id != run->bigapp_start)) {
            run->bigapp_game = id;
            run->game_seen = 1;
            break;
        }
        if (be->now_us(be->user) > deadline) break;
        wait_s(be, &run->opt, 2);
    }
    if (run->game_seen) {
        logf_(run, be, "game running (app id 0x%x); waiting %u s for it to load", (unsigned)run->bigapp_game, run->opt.settle_s);
        wait_s(be, &run->opt, run->opt.settle_s);
        notify(run, be, "AKENO Voice Lab: game tests start in 10 seconds. Keep playing; listen for beeps.");
        wait_s(be, &run->opt, 10);
        phase_tests(be, run, AKN_PHASE_GAME);
        if (!run->opt.no_echo) {
            notify(run, be, "AKENO Voice Lab: echo test - speak now; you may hear yourself after a short delay (10 s)");
            wait_s(be, &run->opt, 2);
            echo(be, run, 10);
        }
        int32_t still = be->bigapp_id(be->user);
        logf_(run, be, "running app id after the game tests: 0x%x (%s)", (unsigned)still,
              still == run->bigapp_game ? "the game kept running" : "CHANGED");
    } else {
        logf_(run, be, "no game was started within %u s: game tests skipped", run->opt.wait_game_s);
    }
    m.stop = 1;
    if (monitoring) pthread_join(mt, NULL);
    pthread_mutex_destroy(&m.lock);
    run->ended_us = be->now_us(be->user);
    notify(run, be, "AKENO Voice Lab finished. The report is in /data/akeno-voicelab/report.txt");
}

/* ---- report ----------------------------------------------------------------------------------- */

typedef struct {
    char *out;
    size_t len, used;
} sb;

static void put(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(sb *b, const char *fmt, ...) {
    if (b->used + 1 >= b->len) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->out + b->used, b->len - b->used, fmt, ap);
    va_end(ap);
    if (n > 0) b->used += (size_t)n < b->len - b->used ? (size_t)n : b->len - b->used - 1;
}

static const char *phase_name(int p) { return p == AKN_PHASE_GAME ? "game" : "home"; }

static const char *tone_verdict(const akn_lab_tone_result *t) {
    if (t->open_rc != 0) return "OPEN_FAILED";
    if (t->ok == 0) return "OUTPUT_FAILED";
    if (t->ok < t->calls) return "PARTLY_ACCEPTED";
    return t->paced ? "ACCEPTED_PACED" : "ACCEPTED_NOT_PACED";
}

size_t akn_lab_summary(const akn_lab_run *run, char *out, size_t len) {
    sb b = { out, len, 0 };
    if (len) out[0] = '\0';
    for (int p = 0; p < AKN_PHASES; p++) {
        if (!run->ran[p]) {
            put(&b, "A.%s.mic=NOT_RUN\n", phase_name(p));
            continue;
        }
        const akn_lab_mic_result *m = &run->mic[p];
        if (m->open_rc != 0) put(&b, "A.%s.mic=OPEN_FAILED code=0x%08x\n", phase_name(p), (unsigned)m->open_rc);
        else
            put(&b, "A.%s.mic=%s rms=%.1fdBFS peak=%d active=%lu/%lu errors=%lu\n", phase_name(p),
                akn_lab_pcm_verdict_name(m->verdict), akn_lab_mic_rms_db(&m->stats), m->stats.peak, m->stats.windows_active,
                m->stats.windows, m->read_errors);
        for (int i = 0; i < AKN_LAB_TONES; i++) {
            const akn_lab_tone_result *t = &run->tones[p][i];
            put(&b, "B.%s.tone%d=%s open=0x%08x ms_per_call=%.1f heard=____\n", phase_name(p), i + 1, tone_verdict(t),
                (unsigned)t->open_rc, t->avg_ms);
        }
    }
    if (run->ran[AKN_PHASE_GAME] && !run->opt.no_echo) {
        const akn_lab_echo_result *e = &run->echo;
        if (e->out_open_rc != 0 || e->mic.open_rc != 0)
            put(&b, "C.game.echo=NOT_RUN out=0x%08x mic=0x%08x\n", (unsigned)e->out_open_rc, (unsigned)e->mic.open_rc);
        else
            put(&b, "C.game.echo=%s mic=%s out_ok=%lu/%lu missing=%llu heard_own_voice=____\n",
                e->out_ok == e->out_calls && e->out_calls ? "RAN" : "OUTPUT_ERRORS", akn_lab_pcm_verdict_name(e->mic.verdict),
                e->out_ok, e->out_calls, e->underruns);
    } else {
        put(&b, "C.game.echo=NOT_RUN\n");
    }
    for (int p = 0; p < AKN_PHASES; p++) {
        const akn_lab_net_result *n = &run->net[p];
        put(&b, "D.%s.heartbeat_gap_max_ms=%.0f daemon=%lu/%lu server=%lu/%lu\n", phase_name(p), n->gap_max_ms, n->daemon_ok,
            n->daemon_ok + n->daemon_fail, n->server_ok, n->server_ok + n->server_fail);
    }
    put(&b, "E.game_audio_still_heard=____ (yes/no)\n");
    put(&b, "game_seen=%s\n", run->game_seen ? "yes" : "no");
    return b.used;
}

size_t akn_lab_report(const akn_lab_run *run, char *out, size_t len) {
    sb b = { out, len, 0 };
    if (len) out[0] = '\0';
    put(&b, "AKENO Voice Lab %s - background audio measurements (EXPERIMENTAL)\n", AKN_LAB_VERSION);
    put(&b, "Nothing was recorded, stored or sent; microphone samples were reduced to the numbers below.\n");
    put(&b, "A call that succeeds is NOT proof of sound: fill in what you heard (the ____ fields).\n\n");
    put(&b, "[Context]\n%s\n", run->context);
    put(&b, "Running app at start: 0x%x; game seen: %s (app id 0x%x)\n", (unsigned)run->bigapp_start,
        run->game_seen ? "yes" : "no", (unsigned)run->bigapp_game);
    put(&b, "sceAudioOutInit: 0x%08x\nDuration: %.0f s\n\n", (unsigned)run->out_init_rc,
        (double)(run->ended_us - run->started_us) / 1e6);
    put(&b, "[Summary]\n");
    char summary[3000];
    akn_lab_summary(run, summary, sizeof(summary));
    put(&b, "%s\n", summary);
    for (int p = 0; p < AKN_PHASES; p++) {
        if (!run->ran[p]) continue;
        const akn_lab_mic_result *m = &run->mic[p];
        put(&b, "[A. Microphone, %s]\n", phase_name(p));
        put(&b, "foreground user query: 0x%08x; open: 0x%08x %s\n", (unsigned)m->user_rc, (unsigned)m->open_rc, m->why);
        put(&b, "reads %lu, errors %lu (first 0x%08x), %.1f ms per read (16 ms expected)\n", m->reads, m->read_errors,
            (unsigned)m->first_error, m->read_ms_avg);
        put(&b, "samples %llu, nonzero %llu, longest zero run %llu, min %d, max %d, peak %d\n", m->stats.samples,
            m->stats.nonzero, m->stats.zero_run_max, m->stats.samples ? m->stats.min : 0, m->stats.samples ? m->stats.max : 0,
            m->stats.peak);
        put(&b, "level %.1f dBFS, loudest 20 ms %.1f dBFS, windows above -45 dBFS: %lu of %lu -> %s\n\n",
            akn_lab_mic_rms_db(&m->stats), m->stats.window_db_max, m->stats.windows_active, m->stats.windows,
            akn_lab_pcm_verdict_name(m->verdict));
        put(&b, "[B. Output, %s]\n", phase_name(p));
        for (int i = 0; i < AKN_LAB_TONES; i++) {
            const akn_lab_tone_result *t = &run->tones[p][i];
            put(&b, "tone %d (%u beep%s) %s: open 0x%08x, %lu/%lu calls ok, first error 0x%08x at call %lu, %.1f ms/call "
                    "(max %.1f; 21.3 expected), close 0x%08x -> %s; heard: ____\n",
                i + 1, t->beeps, t->beeps == 1 ? "" : "s", k_variants[i].label, (unsigned)t->open_rc, t->ok, t->calls, (unsigned)t->first_error,
                t->first_error_call, t->avg_ms, t->max_ms, (unsigned)t->close_rc, tone_verdict(t));
        }
        put(&b, "\n");
    }
    if (run->ran[AKN_PHASE_GAME] && !run->opt.no_echo) {
        const akn_lab_echo_result *e = &run->echo;
        put(&b, "[C. Echo (microphone -> speaker, 400 ms delay) while the game ran]\n");
        put(&b, "output open 0x%08x, %lu/%lu calls ok, %.1f ms per call (16 ms expected)\n", (unsigned)e->out_open_rc, e->out_ok,
            e->out_calls, e->out_avg_ms);
        put(&b, "microphone open 0x%08x, %lu reads, %lu errors, %s, %.1f dBFS; samples missing %llu, dropped %llu\n",
            (unsigned)e->mic.open_rc, e->mic.reads, e->mic.read_errors, akn_lab_pcm_verdict_name(e->mic.verdict),
            akn_lab_mic_rms_db(&e->mic.stats), e->underruns, e->dropped);
        put(&b, "heard your own voice: ____  game sound at the same time: ____\n\n");
    }
    put(&b, "[D. Scheduling and network]\n");
    for (int p = 0; p < AKN_PHASES; p++) {
        const akn_lab_net_result *n = &run->net[p];
        put(&b, "%s: %lu heartbeats, largest gap %.0f ms (250 expected; seconds mean the process was paused); daemon "
                "%lu ok / %lu failed (max %d ms); server %s %lu ok / %lu failed (max %d ms)\n",
            phase_name(p), n->beats, n->gap_max_ms, n->daemon_ok, n->daemon_fail, n->daemon_ms_max,
            run->server_host[0] ? run->server_host : "(not configured)", n->server_ok, n->server_fail, n->server_ms_max);
    }
    put(&b, "\n[Timeline]\n%s", run->log);
    return b.used;
}
