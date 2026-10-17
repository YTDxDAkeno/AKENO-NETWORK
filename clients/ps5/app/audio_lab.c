/*
 * Audio lab v2 (AKENO_EXPERIMENTAL_AUDIO builds only). See audio_lab.h and
 * docs/PS5_AUDIO_RESEARCH.md.
 *
 * Audio lab v1 played a tone that a PS5 with firmware 12.20 accepted (sceAudioOutOutput returned
 * success seven times) while nothing was heard. v2 separates what can be established:
 *   1. a successful API call            init / open / close results
 *   2. samples accepted                 every output call returned >= 0, nonzero PCM submitted
 *   3. samples consumed by the system   output calls blocked for about the block duration, i.e.
 *                                       the audio subsystem paced them at playback speed
 *   4. sound reaching TV or headset     only the user can tell: reported per run on the page
 * Leading hypothesis (docs/PS5_AUDIO_RESEARCH.md): the payload runs in a background system
 * process, not in an application with audio focus, so its port is mixed silently. The delayed
 * test (start, then leave the browser) and the page's own Web Audio test compare against that.
 *
 * Microphone (libSceAudioIn): names are looked up, nothing is ever called. There is no PS5
 * declaration of those functions, and no capture code exists in any AKENO build.
 */
#define _POSIX_C_SOURCE 200809L
#include "audio_lab.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "audio_lab_pcm.h"
#include "platform.h"

#define TONE_FREQUENCY_HZ 440u
#define TONE_LEAD_MS 250u
#define TONE_MS 1500u
#define TONE_TAIL_MS 250u
#define RUNS_KEPT 10
#define RUNS_PER_LAUNCH 30
#define COOLDOWN_MS 3000
#define DELAY_SECONDS 10

typedef struct {
    const char *id;
    const char *label;
    int format;
    unsigned grain;
} lab_preset;

/* Every preset stays inside the configuration of the ps5-payload-dev/SDL audio driver. */
static const lab_preset k_presets[] = {
    { "stereo16-1024", "S16 stereo, 1024 frames per call (the SDL driver's usual path)", AUDIO_LAB_FORMAT_S16_STEREO, 1024 },
    { "mono16-256", "S16 mono, 256 frames per call (as audio lab v1, for comparison)", AUDIO_LAB_FORMAT_S16_MONO, 256 },
    { "stereofloat-1024", "Float stereo, 1024 frames per call", AUDIO_LAB_FORMAT_FLOAT_STEREO, 1024 },
    { "stereo16-2048", "S16 stereo, 2048 frames per call (largest block)", AUDIO_LAB_FORMAT_S16_STEREO, 2048 },
};

typedef struct {
    const char *id;
    const char *label;
    double amplitude;
} lab_level;

static const lab_level k_levels[] = {
    { "quiet", "Quiet (-26 dBFS)", 0.05 },
    { "moderate", "Moderate (-20 dBFS)", 0.1 },
    { "clear", "Clearer (-14 dBFS)", 0.2 },
};

typedef struct {
    const char *library;
    const char *names[12];
    size_t count;
    const char *source;
    const char *use;
} probe_library;

/*
 * libSceAudioOut: the four functions the SDL driver calls, and (looked up only) names that say
 * something about routing, volume and application focus; none of them has a PS5 declaration.
 * libSceAudioIn: not in the SDK; the names are those of the PS4's capture interface.
 */
static const probe_library k_libraries[] = {
    { "libSceAudioOut.sprx", { "sceAudioOutInit", "sceAudioOutOpen", "sceAudioOutOutput", "sceAudioOutClose" }, 4,
      "ps5-payload-sdk stub + ps5-payload-dev/SDL", "used by the output test" },
    { "libSceAudioOut.sprx",
      { "sceAudioOutGetPortState", "sceAudioOutSetVolume", "sceAudioOutGetLastOutputTime", "sceAudioOutGetHandleStatusInfo",
        "sceAudioOutGetSystemState", "sceAudioOutSetMainOutput", "sceAudioOutGetFocusEnablePid", "sceAudioOutAttachToApplicationByPid",
        "sceAudioOutDetachFromApplicationByPid", "sceAudioOutChangeAppModuleState" },
      10, "ps5-payload-sdk stub (names only, no declarations)", "looked up only, never called" },
    { "libSceAudioIn.sprx", { "sceAudioInOpen", "sceAudioInInput", "sceAudioInClose" }, 3, "PS4 homebrew stubs (not in the PS5 SDK)",
      "looked up only, never called (no microphone access)" },
};

typedef struct {
    int id;
    long long started_at; /* epoch seconds */
    const lab_preset *preset;
    const lab_level *level;
    int delay_seconds;
    unsigned long frames_planned;
    int rc;
    akeno_audio_result result;
    char outcome[16];
    int paced;      /* 1 consumed at playback speed, 0 faster than real time, -1 not enough calls */
    int feedback;   /* 1 heard, 0 not heard, -1 not reported */
    char route[24];
} lab_run;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_report_path[600] = "";
static char g_build[80] = "";
static char g_firmware[16] = "";
static char *g_probe_json = NULL;   /* g_lock */
static char *g_probe_text = NULL;   /* g_lock: the same probe for the text report */
static lab_run g_runs[RUNS_KEPT];   /* g_lock: ring, by id */
static int g_next_run = 1;          /* g_lock */
static int g_running = 0;           /* g_lock: waiting or playing */
static int g_playing = 0;           /* g_lock */
static long long g_wait_until = 0;  /* g_lock: monotonic ms when a delayed test starts */
static long long g_last_end = 0;    /* g_lock: monotonic ms */
static volatile int g_cancel = 0;
static pthread_t g_thread;
static int g_thread_started = 0;    /* main thread only */
static audio_lab_browser_result g_browser; /* g_lock */
static int g_browser_reported = 0;  /* g_lock */

static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void copy_str(char *dst, size_t len, const char *src) { snprintf(dst, len, "%s", src ? src : ""); }

static const lab_preset *find_preset(const char *id) {
    if (!id || !id[0]) return &k_presets[0];
    for (size_t i = 0; i < sizeof(k_presets) / sizeof(k_presets[0]); i++)
        if (strcmp(k_presets[i].id, id) == 0) return &k_presets[i];
    return NULL;
}

static const lab_level *find_level(const char *id) {
    if (!id || !id[0]) return &k_levels[1];
    for (size_t i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++)
        if (strcmp(k_levels[i].id, id) == 0) return &k_levels[i];
    return NULL;
}

static lab_run *run_by_id(int id) {
    if (id <= 0) return NULL;
    lab_run *r = &g_runs[id % RUNS_KEPT];
    return r->id == id ? r : NULL;
}

static const char *route_label(const char *route) {
    if (strcmp(route, "tv") == 0) return "TV / HDMI";
    if (strcmp(route, "controller-headset") == 0) return "headset on the controller";
    if (strcmp(route, "usb-headset") == 0) return "USB headset";
    if (strcmp(route, "other") == 0) return "other";
    return "not said";
}

/* ---- Report file --------------------------------------------------------------------------- */

static void write_report_locked(void);

void audio_lab_init(const char *report_path, const char *build, const char *firmware) {
    pthread_mutex_lock(&g_lock);
    copy_str(g_report_path, sizeof(g_report_path), report_path);
    copy_str(g_build, sizeof(g_build), build);
    copy_str(g_firmware, sizeof(g_firmware), firmware);
    memset(&g_browser, 0, sizeof(g_browser));
    g_browser.heard = -1;
    pthread_mutex_unlock(&g_lock);
}

/* ---- Probe (prototype A) ------------------------------------------------------------------- */

static const char *const k_candidate_files[] = {
    "libSceAudioOut.sprx", "libSceAudioIn.sprx", "libSceAudio3d.sprx", "libSceAudiodec.sprx", "libSceVoice.sprx", "libSceVoiceQoS.sprx",
};

void audio_lab_probe(void) {
    akeno_buf b, t;
    akeno_buf_init(&b, 64 * 1024);
    akeno_buf_init(&t, 32 * 1024);
    const char *dir = akeno_platform_audio_library_dir();
    struct stat st;
    int readable = dir && stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
    akeno_buf_printf(&b, "{\"ranAt\":%lld,\"libraryDir\":", (long long)time(NULL));
    akeno_buf_append_json_string(&b, dir ? dir : "");
    akeno_buf_printf(&b, ",\"libraryDirReadable\":%s,\"audioFiles\":[", readable ? "true" : "false");
    akeno_buf_printf(&t, "Library folder: %s (%s)\n  Files present:", dir ? dir : "unknown", readable ? "readable" : "not readable");
    int first = 1;
    for (size_t i = 0; readable && i < sizeof(k_candidate_files) / sizeof(k_candidate_files[0]); i++) {
        char path[700];
        snprintf(path, sizeof(path), "%s/%s", dir, k_candidate_files[i]);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (!first) akeno_buf_append_str(&b, ",");
        akeno_buf_append_json_string(&b, k_candidate_files[i]);
        akeno_buf_printf(&t, " %s", k_candidate_files[i]);
        first = 0;
    }
    if (first) akeno_buf_append_str(&t, " none found");
    akeno_buf_printf(&b, "],\"filesChecked\":%zu,\"libraries\":[", sizeof(k_candidate_files) / sizeof(k_candidate_files[0]));
    akeno_buf_append_str(&t, "\n");
    for (size_t l = 0; l < sizeof(k_libraries) / sizeof(k_libraries[0]); l++) {
        const probe_library *lib = &k_libraries[l];
        int found[12] = { 0 };
        char err[200] = "";
        int loaded = akeno_platform_audio_lookup(lib->library, lib->names, found, lib->count, err, sizeof(err)) == 0;
        if (l) akeno_buf_append_str(&b, ",");
        akeno_buf_append_str(&b, "{\"name\":");
        akeno_buf_append_json_string(&b, lib->library);
        akeno_buf_append_str(&b, ",\"namesFrom\":");
        akeno_buf_append_json_string(&b, lib->source);
        akeno_buf_append_str(&b, ",\"use\":");
        akeno_buf_append_json_string(&b, lib->use);
        akeno_buf_printf(&b, ",\"loaded\":%s,\"error\":", loaded ? "true" : "false");
        akeno_buf_append_json_string(&b, loaded ? "" : err);
        akeno_buf_append_str(&b, ",\"symbols\":[");
        akeno_buf_printf(&t, "%s (%s): %s\n   ", lib->library, lib->use, loaded ? "loads" : err);
        for (size_t i = 0; i < lib->count; i++) {
            if (i) akeno_buf_append_str(&b, ",");
            akeno_buf_append_str(&b, "{\"name\":");
            akeno_buf_append_json_string(&b, lib->names[i]);
            akeno_buf_printf(&b, ",\"exported\":%s}", found[i] ? "true" : "false");
            akeno_buf_printf(&t, " %s %s", lib->names[i], found[i] ? "[exported]" : "[missing]");
        }
        akeno_buf_append_str(&b, "]}");
        akeno_buf_append_str(&t, "\n");
    }
    akeno_buf_append_str(&b, "],\"microphoneEnumeration\":");
    akeno_buf_append_json_string(&b, "not attempted: no verified PS5 declaration of libSceAudioIn exists; its names are looked up "
                                     "only and nothing is called, recorded or sent");
    akeno_buf_append_str(&b, "}");

    pthread_mutex_lock(&g_lock);
    free(g_probe_json);
    free(g_probe_text);
    g_probe_json = b.data ? strdup(b.data) : NULL;
    g_probe_text = t.data ? strdup(t.data) : NULL;
    write_report_locked();
    pthread_mutex_unlock(&g_lock);
    akeno_buf_free(&b);
    akeno_buf_free(&t);
}

/* ---- Output test (prototype C, v2) --------------------------------------------------------- */

typedef struct {
    audio_lab_osc osc;
    int format;
} fill_ctx;

static unsigned long fill_block(void *user, void *block, unsigned frames, double *peak) {
    fill_ctx *c = user;
    audio_lab_block_stats stats;
    audio_lab_fill(&c->osc, c->format, block, frames, &stats);
    *peak = stats.peak;
    return stats.nonzero;
}

static const char *classify(int rc, const akeno_audio_result *r) {
    if (rc == -2) return "not-supported";
    if (!r->library_loaded) return "not-supported";
    if (r->init_rc != 0) return "init-failed";
    if (!r->open_called || r->open_rc < 1) return "open-failed";
    if (r->first_error_rc < 0) return "submit-failed";
    if (r->cancelled) return "cancelled";
    (void)rc; /* A failed close after accepted samples stays "accepted"; its result is in the details. */
    return "accepted";
}

/* Consumed at playback speed when calls 2..n took at least 70 % of the audio they carried. */
static int paced(const akeno_audio_result *r, unsigned grain) {
    if (r->calls_ok < 4) return -1;
    double expected_us = (double)(r->calls_ok - 1) * (double)grain * 1e6 / (double)AUDIO_LAB_RATE;
    return (double)r->call_total_us >= 0.7 * expected_us ? 1 : 0;
}

static void *tone_worker(void *arg) {
    int id = (int)(long)arg;
    pthread_mutex_lock(&g_lock);
    lab_run *run = run_by_id(id);
    long long wait_until = g_wait_until;
    const lab_preset *preset = run->preset;
    const lab_level *level = run->level;
    unsigned long frames = run->frames_planned;
    pthread_mutex_unlock(&g_lock);

    while (!g_cancel && mono_ms() < wait_until) {
        struct timespec ts = { 0, 100 * 1000000L };
        nanosleep(&ts, NULL);
    }
    akeno_audio_result result;
    memset(&result, 0, sizeof(result));
    int rc = 0;
    if (g_cancel) {
        result.cancelled = 1;
        copy_str(result.error, sizeof(result.error), "Cancelled before it started.");
    } else {
        pthread_mutex_lock(&g_lock);
        g_playing = 1;
        pthread_mutex_unlock(&g_lock);
        fill_ctx ctx;
        ctx.format = preset->format;
        audio_lab_osc_init(&ctx.osc, TONE_FREQUENCY_HZ, level->amplitude, (unsigned long)AUDIO_LAB_RATE * TONE_LEAD_MS / 1000u,
                           (unsigned long)AUDIO_LAB_RATE * TONE_MS / 1000u);
        akeno_audio_params p;
        memset(&p, 0, sizeof(p));
        p.user_id = 0xFF; /* SCE_USER_SERVICE_USER_ID_SYSTEM, as the SDL driver */
        p.port_type = 0;  /* main port, as the SDL driver */
        p.format = preset->format;
        p.grain = preset->grain;
        p.rate = AUDIO_LAB_RATE;
        p.frames = frames;
        p.fill = fill_block;
        p.fill_user = &ctx;
        p.cancel = &g_cancel;
        rc = akeno_platform_audio_run(&p, &result);
    }
    pthread_mutex_lock(&g_lock);
    run = run_by_id(id);
    if (run) {
        run->rc = rc;
        run->result = result;
        copy_str(run->outcome, sizeof(run->outcome), g_cancel && !result.open_called ? "cancelled" : classify(rc, &result));
        run->paced = paced(&result, preset->grain);
    }
    g_running = 0;
    g_playing = 0;
    g_wait_until = 0;
    g_last_end = mono_ms();
    write_report_locked();
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

int audio_lab_tone_start(const char *preset_id, const char *level_id, int delay_seconds, char *err, size_t err_len) {
    const lab_preset *preset = find_preset(preset_id);
    const lab_level *level = find_level(level_id);
    if (!preset || !level || (delay_seconds != 0 && delay_seconds != DELAY_SECONDS)) {
        snprintf(err, err_len, "Unknown test settings.");
        return -1;
    }
    pthread_mutex_lock(&g_lock);
    if (g_running) {
        pthread_mutex_unlock(&g_lock);
        snprintf(err, err_len, "A test is already waiting or playing.");
        return 1;
    }
    if (g_last_end && mono_ms() - g_last_end < COOLDOWN_MS) {
        pthread_mutex_unlock(&g_lock);
        snprintf(err, err_len, "Wait a few seconds between tests.");
        return 2;
    }
    if (g_next_run > RUNS_PER_LAUNCH) {
        pthread_mutex_unlock(&g_lock);
        snprintf(err, err_len, "Test limit for this launch reached. Restart the audio lab for more.");
        return 3;
    }
    pthread_mutex_unlock(&g_lock);
    /* The previous worker has finished (not running); collect it before starting another. */
    if (g_thread_started) {
        pthread_join(g_thread, NULL);
        g_thread_started = 0;
    }
    pthread_mutex_lock(&g_lock);
    int id = g_next_run++;
    lab_run *run = &g_runs[id % RUNS_KEPT];
    memset(run, 0, sizeof(*run));
    run->id = id;
    run->started_at = (long long)time(NULL);
    run->preset = preset;
    run->level = level;
    run->delay_seconds = delay_seconds;
    unsigned long frames = (unsigned long)AUDIO_LAB_RATE * (TONE_LEAD_MS + TONE_MS + TONE_TAIL_MS) / 1000u;
    run->frames_planned = (frames + preset->grain - 1) / preset->grain * preset->grain;
    run->paced = -1;
    run->feedback = -1;
    copy_str(run->outcome, sizeof(run->outcome), delay_seconds ? "waiting" : "playing");
    g_running = 1;
    g_playing = delay_seconds == 0; /* An immediate test reports "playing" from the start. */
    g_cancel = 0;
    g_wait_until = mono_ms() + (long long)delay_seconds * 1000;
    pthread_mutex_unlock(&g_lock);
    if (pthread_create(&g_thread, NULL, tone_worker, (void *)(long)id) != 0) {
        pthread_mutex_lock(&g_lock);
        g_running = 0;
        copy_str(run->outcome, sizeof(run->outcome), "not-started");
        copy_str(run->result.error, sizeof(run->result.error), "Could not start a thread.");
        pthread_mutex_unlock(&g_lock);
        snprintf(err, err_len, "Could not start a thread.");
        return -2;
    }
    g_thread_started = 1;
    return 0;
}

int audio_lab_cancel(void) {
    pthread_mutex_lock(&g_lock);
    int running = g_running;
    if (running) g_cancel = 1;
    pthread_mutex_unlock(&g_lock);
    return running ? 0 : -1;
}

int audio_lab_feedback(int run_id, int heard, const char *route) {
    pthread_mutex_lock(&g_lock);
    lab_run *run = run_by_id(run_id);
    int ok = run && !(g_running && run_id == g_next_run - 1);
    if (ok) {
        run->feedback = heard ? 1 : 0;
        copy_str(run->route, sizeof(run->route), heard ? route : "");
        write_report_locked();
    }
    pthread_mutex_unlock(&g_lock);
    return ok ? 0 : -1;
}

void audio_lab_browser_report(const audio_lab_browser_result *result) {
    pthread_mutex_lock(&g_lock);
    g_browser = *result;
    g_browser_reported = 1;
    write_report_locked();
    pthread_mutex_unlock(&g_lock);
}

/* ---- Status JSON and report ---------------------------------------------------------------- */

/* Headline over all runs; caller holds g_lock. */
static const char *headline_locked(int *run_out) {
    *run_out = 0;
    const lab_run *latest = NULL;
    for (int i = 0; i < RUNS_KEPT; i++) {
        const lab_run *r = &g_runs[i];
        if (!r->id) continue;
        if (r->feedback == 1 && (!*run_out || r->id > *run_out)) *run_out = r->id;
        if (!latest || r->id > latest->id) latest = r;
    }
    if (g_running) {
        *run_out = latest ? latest->id : 0;
        return g_playing ? "playing" : "waiting";
    }
    if (*run_out) return "audible-confirmed";
    if (!latest) return "not-run";
    *run_out = latest->id;
    if (strcmp(latest->outcome, "accepted") == 0 && latest->feedback == 0) return "accepted-not-heard";
    return latest->outcome;
}

static void run_json(akeno_buf *b, const lab_run *r) {
    const akeno_audio_result *x = &r->result;
    akeno_buf_printf(b, "{\"id\":%d,\"startedAt\":%lld,\"preset\":", r->id, r->started_at);
    akeno_buf_append_json_string(b, r->preset->id);
    akeno_buf_append_str(b, ",\"presetLabel\":");
    akeno_buf_append_json_string(b, r->preset->label);
    akeno_buf_append_str(b, ",\"level\":");
    akeno_buf_append_json_string(b, r->level->id);
    akeno_buf_append_str(b, ",\"levelLabel\":");
    akeno_buf_append_json_string(b, r->level->label);
    akeno_buf_printf(b, ",\"delaySeconds\":%d,\"userId\":\"0x%02x\",\"portType\":0,\"rate\":%u,\"channels\":%u,\"format\":",
                     r->delay_seconds, 0xFF, AUDIO_LAB_RATE, audio_lab_format_channels(r->preset->format));
    akeno_buf_append_json_string(b, audio_lab_format_name(r->preset->format));
    akeno_buf_printf(b, ",\"formatParam\":%d,\"grain\":%u,\"framesPlanned\":%lu,\"toneHz\":%u,\"toneMs\":%u,\"leadMs\":%u,\"tailMs\":%u,"
                        "\"amplitude\":%.3f,\"outcome\":",
                     r->preset->format, r->preset->grain, r->frames_planned, TONE_FREQUENCY_HZ, TONE_MS, TONE_LEAD_MS, TONE_TAIL_MS,
                     r->level->amplitude);
    akeno_buf_append_json_string(b, r->outcome);
    akeno_buf_printf(b,
                     ",\"libraryLoaded\":%s,\"initCalled\":%s,\"initResult\":\"0x%08x\",\"openCalled\":%s,\"openResult\":\"0x%08x\","
                     "\"handle\":%d,\"calls\":%lu,\"callsOk\":%lu,\"firstErrorResult\":\"0x%08x\",\"firstErrorCall\":%lu,"
                     "\"lastOutputResult\":\"0x%08x\",\"framesSubmitted\":%lu,\"nonzeroFrames\":%lu,\"pcmNonzero\":%s,\"peak\":%.4f,"
                     "\"submittedMs\":%lu,\"wallMs\":%.1f,\"callMinMs\":%.2f,\"callAvgMs\":%.2f,\"callMaxMs\":%.2f,\"blockMs\":%.2f,"
                     "\"paced\":%s,\"bufferAligned\":%s,\"closeCalled\":%s,\"closeResult\":\"0x%08x\",\"cancelled\":%s,\"error\":",
                     x->library_loaded ? "true" : "false", x->init_called ? "true" : "false", (unsigned)x->init_rc,
                     x->open_called ? "true" : "false", (unsigned)x->open_rc, x->open_rc > 0 ? x->open_rc : -1, x->calls, x->calls_ok,
                     (unsigned)x->first_error_rc, x->first_error_call, (unsigned)x->last_output_rc, x->frames_submitted, x->nonzero_frames,
                     x->nonzero_frames > 0 ? "true" : "false", x->peak, x->frames_submitted * 1000u / AUDIO_LAB_RATE,
                     (double)x->wall_us / 1000.0, (double)x->call_min_us / 1000.0,
                     x->calls_ok > 1 ? (double)x->call_total_us / 1000.0 / (double)(x->calls_ok - 1) : 0.0, (double)x->call_max_us / 1000.0,
                     (double)r->preset->grain * 1000.0 / (double)AUDIO_LAB_RATE,
                     r->paced < 0 ? "null" : r->paced ? "true" : "false", x->buffer_aligned ? "true" : "false",
                     x->close_called ? "true" : "false", (unsigned)x->close_rc, x->cancelled ? "true" : "false");
    akeno_buf_append_json_string(b, x->error);
    akeno_buf_append_str(b, ",\"heard\":");
    akeno_buf_append_str(b, r->feedback < 0 ? "null" : r->feedback ? "true" : "false");
    akeno_buf_append_str(b, ",\"route\":");
    akeno_buf_append_json_string(b, r->route);
    akeno_buf_append_str(b, "}");
}

void audio_lab_status_json(akeno_buf *b) {
    pthread_mutex_lock(&g_lock);
    int headline_run = 0;
    const char *state = headline_locked(&headline_run);
    long long now = mono_ms();
    akeno_buf_append_str(b, "{\"experimental\":true,\"version\":2,\"state\":");
    akeno_buf_append_json_string(b, state);
    akeno_buf_printf(b, ",\"stateRun\":%d,\"running\":%s,\"playing\":%s,\"secondsUntilStart\":%lld,\"runsLeft\":%d,\"cooldownMs\":%lld",
                     headline_run, g_running ? "true" : "false", g_playing ? "true" : "false",
                     g_running && !g_playing && g_wait_until > now ? (g_wait_until - now + 999) / 1000 : 0,
                     RUNS_PER_LAUNCH - (g_next_run - 1),
                     g_last_end && now - g_last_end < COOLDOWN_MS ? COOLDOWN_MS - (now - g_last_end) : 0);
    akeno_buf_append_str(b, ",\"firmware\":");
    akeno_buf_append_json_string(b, g_firmware);
    akeno_buf_append_str(b, ",\"presets\":[");
    for (size_t i = 0; i < sizeof(k_presets) / sizeof(k_presets[0]); i++) {
        if (i) akeno_buf_append_str(b, ",");
        akeno_buf_append_str(b, "{\"id\":");
        akeno_buf_append_json_string(b, k_presets[i].id);
        akeno_buf_append_str(b, ",\"label\":");
        akeno_buf_append_json_string(b, k_presets[i].label);
        akeno_buf_append_str(b, "}");
    }
    akeno_buf_append_str(b, "],\"levels\":[");
    for (size_t i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++) {
        if (i) akeno_buf_append_str(b, ",");
        akeno_buf_append_str(b, "{\"id\":");
        akeno_buf_append_json_string(b, k_levels[i].id);
        akeno_buf_append_str(b, ",\"label\":");
        akeno_buf_append_json_string(b, k_levels[i].label);
        akeno_buf_append_str(b, "}");
    }
    akeno_buf_printf(b, "],\"delaySeconds\":%d,\"routing\":", DELAY_SECONDS);
    akeno_buf_append_json_string(b, "Main port (type 0) of the system user (0xFF): the system decides where it plays (TV/HDMI, or a "
                                    "headset when the console routes all audio there). No verified interface lists or selects outputs.");
    akeno_buf_append_str(b, ",\"portState\":");
    akeno_buf_append_json_string(b, "not read: sceAudioOutGetPortState exists only as a name (no verified PS5 declaration)");
    akeno_buf_append_str(b, ",\"probe\":");
    akeno_buf_append_str(b, g_probe_json ? g_probe_json : "null");
    akeno_buf_append_str(b, ",\"runs\":[");
    int first = 1;
    for (int id = g_next_run - 1; id >= 1 && id > g_next_run - 1 - RUNS_KEPT; id--) {
        const lab_run *r = run_by_id(id);
        if (!r) continue;
        if (!first) akeno_buf_append_str(b, ",");
        run_json(b, r);
        first = 0;
    }
    akeno_buf_append_str(b, "],\"browser\":");
    if (g_browser_reported) {
        akeno_buf_printf(b, "{\"apiAvailable\":%s,\"state\":", g_browser.api_available ? "true" : "false");
        akeno_buf_append_json_string(b, g_browser.state);
        akeno_buf_printf(b, ",\"sampleRate\":%d,\"clockAdvanced\":%s,\"heard\":%s,\"route\":", g_browser.sample_rate,
                         g_browser.clock_advanced ? "true" : "false", g_browser.heard < 0 ? "null" : g_browser.heard ? "true" : "false");
        akeno_buf_append_json_string(b, g_browser.route);
        akeno_buf_append_str(b, ",\"error\":");
        akeno_buf_append_json_string(b, g_browser.error);
        akeno_buf_append_str(b, "}");
    } else {
        akeno_buf_append_str(b, "null");
    }
    akeno_buf_append_str(b, ",\"reportFile\":");
    akeno_buf_append_json_string(b, g_report_path);
    akeno_buf_append_str(b, "}");
    pthread_mutex_unlock(&g_lock);
}

static const char *state_sentence(const char *state) {
    if (strcmp(state, "not-run") == 0) return "No output test has run yet.";
    if (strcmp(state, "not-supported") == 0) return "Not supported: the audio output functions are not available.";
    if (strcmp(state, "init-failed") == 0) return "Failed to initialize the audio output module.";
    if (strcmp(state, "open-failed") == 0) return "Failed to open an audio output port.";
    if (strcmp(state, "submit-failed") == 0) return "Failed to submit samples.";
    if (strcmp(state, "accepted") == 0) return "Samples accepted by the system. Audible output NOT confirmed.";
    if (strcmp(state, "accepted-not-heard") == 0) return "Samples accepted by the system, but the user heard nothing.";
    if (strcmp(state, "audible-confirmed") == 0) return "Audible output confirmed by the user.";
    if (strcmp(state, "cancelled") == 0) return "The last test was cancelled.";
    if (strcmp(state, "waiting") == 0) return "A delayed test is waiting to start.";
    if (strcmp(state, "playing") == 0) return "A test is playing.";
    return state;
}

static void run_text(akeno_buf *t, const lab_run *r) {
    const akeno_audio_result *x = &r->result;
    char when[40] = "";
    time_t at = (time_t)r->started_at;
    struct tm tm;
    if (gmtime_r(&at, &tm)) strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", &tm);
    akeno_buf_printf(t, "Run #%d  %s  delay %d s\n", r->id, when, r->delay_seconds);
    akeno_buf_printf(t, "  Request: user 0xFF (system), port type 0 (main), %u Hz, %s (param %d, %u ch), %u frames/call, %u Hz tone %u ms "
                        "(+%u ms silence before, %u ms after), %s\n",
                     AUDIO_LAB_RATE, audio_lab_format_name(r->preset->format), r->preset->format,
                     audio_lab_format_channels(r->preset->format), r->preset->grain, TONE_FREQUENCY_HZ, TONE_MS, TONE_LEAD_MS, TONE_TAIL_MS,
                     r->level->label);
    akeno_buf_printf(t, "  Library: %s   sceAudioOutInit: %s0x%08x   sceAudioOutOpen: %s\n", x->library_loaded ? "loaded" : "not loaded",
                     x->init_called ? "" : "(earlier call) ", (unsigned)x->init_rc,
                     !x->open_called ? "not called" : x->open_rc > 0 ? "port opened" : "failed");
    if (x->open_called) akeno_buf_printf(t, "    open result 0x%08x (handle %d)\n", (unsigned)x->open_rc, x->open_rc > 0 ? x->open_rc : -1);
    akeno_buf_printf(t, "  sceAudioOutOutput: %lu of %lu calls succeeded; first error 0x%08x (call %lu); last result 0x%08x\n", x->calls_ok,
                     x->calls, (unsigned)x->first_error_rc, x->first_error_call, (unsigned)x->last_output_rc);
    akeno_buf_printf(t, "  Submitted: %lu frames (%lu ms of audio), PCM nonzero: %s (%lu frames, peak %.3f), buffer 64-byte aligned: %s\n",
                     x->frames_submitted, x->frames_submitted * 1000u / AUDIO_LAB_RATE, x->nonzero_frames ? "yes" : "NO", x->nonzero_frames,
                     x->peak, x->buffer_aligned ? "yes" : "no");
    akeno_buf_printf(t, "  Timing: %.1f ms from first to last call; per call (after the first) min %.2f / avg %.2f / max %.2f ms; one block "
                        "= %.2f ms -> %s\n",
                     (double)x->wall_us / 1000.0, (double)x->call_min_us / 1000.0,
                     x->calls_ok > 1 ? (double)x->call_total_us / 1000.0 / (double)(x->calls_ok - 1) : 0.0, (double)x->call_max_us / 1000.0,
                     (double)r->preset->grain * 1000.0 / (double)AUDIO_LAB_RATE,
                     r->paced < 0 ? "not enough calls to judge"
                     : r->paced  ? "consumed at playback speed (the system paced the samples)"
                                 : "returned faster than real time (samples were not paced)");
    akeno_buf_printf(t, "  sceAudioOutClose: %s0x%08x\n", x->close_called ? "" : "(not called) ", (unsigned)x->close_rc);
    akeno_buf_printf(t, "  Outcome: %s%s%s\n", r->outcome, x->error[0] ? " - " : "", x->error);
    akeno_buf_printf(t, "  User report: %s%s%s\n", r->feedback < 0 ? "not reported" : r->feedback ? "HEARD" : "not heard",
                     r->feedback == 1 ? " on " : "", r->feedback == 1 ? route_label(r->route) : "");
}

/* Caller holds g_lock. */
static void report_locked(akeno_buf *t) {
    int headline_run = 0;
    const char *state = headline_locked(&headline_run);
    char now[40] = "";
    time_t t_now = time(NULL);
    struct tm tm;
    if (gmtime_r(&t_now, &tm)) strftime(now, sizeof(now), "%Y-%m-%dT%H:%M:%SZ", &tm);
    akeno_buf_printf(t, "AKENO NETWORK audio lab v2 report\nBuild %s, firmware %s, generated %s\n", g_build, g_firmware[0] ? g_firmware : "unknown",
                     now);
    akeno_buf_append_str(t, "Contains no account data, tokens or credentials. No audio was recorded.\n\n");
    akeno_buf_printf(t, "STATE: %s%s\n", state_sentence(state), headline_run && strcmp(state, "audible-confirmed") == 0 ? " (see the run below)" : "");
    akeno_buf_append_str(t, "Route: main port of the system user; the system chooses where it plays. Port state, volume and routing are not "
                            "read (no verified declarations).\n\n");
    akeno_buf_append_str(t, "LIBRARIES (read-only probe)\n");
    akeno_buf_append_str(t, g_probe_text ? g_probe_text : "  Not run yet (Check audio libraries).\n");
    akeno_buf_append_str(t, "\nOUTPUT TESTS (newest first)\n");
    int any = 0;
    for (int id = g_next_run - 1; id >= 1 && id > g_next_run - 1 - RUNS_KEPT; id--) {
        const lab_run *r = run_by_id(id);
        if (!r) continue;
        run_text(t, r);
        any = 1;
    }
    if (!any) akeno_buf_append_str(t, "  None yet.\n");
    akeno_buf_append_str(t, "\nBROWSER AUDIO (Web Audio API in the system browser, for comparison)\n");
    if (g_browser_reported) {
        akeno_buf_printf(t, "  API: %s, context state: %s, sample rate: %d, clock advanced: %s, user report: %s%s%s%s%s\n",
                         g_browser.api_available ? "available" : "NOT available", g_browser.state, g_browser.sample_rate,
                         g_browser.clock_advanced ? "yes" : "no",
                         g_browser.heard < 0 ? "not reported" : g_browser.heard ? "HEARD" : "not heard", g_browser.heard == 1 ? " on " : "",
                         g_browser.heard == 1 ? route_label(g_browser.route) : "", g_browser.error[0] ? "; error: " : "", g_browser.error);
    } else {
        akeno_buf_append_str(t, "  Not run yet.\n");
    }
    akeno_buf_append_str(t, "\nMICROPHONE: not attempted. libSceAudioIn names are looked up only; no capture code exists.\n");
}

void audio_lab_report_text(akeno_buf *b) {
    pthread_mutex_lock(&g_lock);
    report_locked(b);
    pthread_mutex_unlock(&g_lock);
}

/* Writes the report next to the settings (mode 0600) so it can be copied off the console. */
static void write_report_locked(void) {
    if (!g_report_path[0]) return;
    akeno_buf t;
    akeno_buf_init(&t, 64 * 1024);
    report_locked(&t);
    char tmp[620];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_report_path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        int ok = t.data && write(fd, t.data, t.len) == (ssize_t)t.len;
        ok = close(fd) == 0 && ok;
        if (!ok || rename(tmp, g_report_path) != 0) unlink(tmp);
    }
    akeno_buf_free(&t);
}

void audio_lab_shutdown(void) {
    audio_lab_cancel();
    if (g_thread_started) pthread_join(g_thread, NULL);
    g_thread_started = 0;
    pthread_mutex_lock(&g_lock);
    free(g_probe_json);
    free(g_probe_text);
    g_probe_json = NULL;
    g_probe_text = NULL;
    pthread_mutex_unlock(&g_lock);
}
