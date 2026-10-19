/*
 * A simulated console for the PC tests of AKENO Party Voice. Nothing here is evidence about a PS5.
 *
 *   PV_SIM_MIC      tone:<hz> (default tone:300) | silence | refused | missing | process-refused | rate-refused |
 *                   fail-after:<reads>
 *   PV_SIM_OUT      paced (default) | unpaced | refused | queue | user-refused | fail-after:<calls>
 *   PV_SIM_CONTEXT  background (default, like the voice service on the PS5) | foreground | unknown
 *   PV_SIM_BIGAPP_FILE  a file whose content is the running app ID (written by the test to "start a game")
 *
 * The microphone delivers a 16 kHz tone in real time (each read blocks for one grain, like the
 * console's). "paced" blocks every output call for one block; "queue" models a console port: a
 * device clock drains a queue of two blocks, so a call returns at once while the queue has room
 * and blocks once it is full, and a caller that sleeps between calls never sees a call block.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pv_backend.h"
#include "voice_dsp.h"

#define SIM_QUEUE_BLOCKS 2
#define SIM_HANDLE 7
#define SIM_MIC_HANDLE 3

static uint64_t h_now_us(void *u) {
    (void)u;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void sleep_us(uint64_t us) {
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    while (nanosleep(&ts, &ts) != 0) {
    }
}

static int h_notify(void *u, const char *text) {
    pv_sim *s = u;
    s->notifications++;
    pthread_mutex_lock(&s->lock);
    snprintf(s->last_notification, sizeof(s->last_notification), "%s", text);
    pthread_mutex_unlock(&s->lock);
    fprintf(stderr, "[notification] %s\n", text);
    return 0;
}

static int32_t h_bigapp(void *u) {
    pv_sim *s = u;
    if (!s->bigapp_file[0]) return -1;
    FILE *f = fopen(s->bigapp_file, "r");
    if (!f) return -1;
    long id = -1;
    if (fscanf(f, "%li", &id) != 1) id = -1;
    fclose(f);
    return (int32_t)id;
}

static void h_describe(void *u, char *out, size_t len) {
    (void)u;
    snprintf(out, len, "PC (simulated console), pid %d", (int)getpid());
}

/* The app info answer as the PS5 gives it (app ID in the first four bytes, little endian). */
static void h_process(void *u, akn_context_raw *raw) {
    pv_sim *s = u;
    memset(raw, 0, sizeof(*raw));
    int mode = s->context;
    int32_t big = h_bigapp(u);
    if (mode == 1 && big <= 0) big = 0x60000200; /* "foreground": this process is the running app */
    raw->have_bigapp_fn = 1;
    raw->bigapp_app_id = big;
    raw->have_appinfo_fn = mode != 2;
    uint32_t own = mode == 1 ? (uint32_t)big : 0;
    for (int i = 0; i < 4; i++) raw->appinfo_raw[i] = (unsigned char)(own >> (8 * i));
    akn_context_decode(raw);
}

static int32_t h_foreground_user(void *u, int32_t *user) {
    (void)u;
    *user = 0x10000001;
    return 0;
}

static int32_t h_out_init(void *u) {
    (void)u;
    return 0;
}

static int32_t h_out_open(void *u, int route, char *why, size_t why_len) {
    pv_sim *s = u;
    const pv_route *r = pv_route_get(route);
    if (!r || r->retired) return (int32_t)0x80260001;
    s->out_open_calls++;
    if (s->out_mode == 2) {
        snprintf(why, why_len, "simulated output refused");
        return (int32_t)0x8026000A;
    }
    if (s->out_mode == 4 && r->foreground_user) {
        snprintf(why, why_len, "simulated: the foreground user's port refused, as on the PS5");
        return (int32_t)0x80260011;
    }
    s->out_opens++;
    pthread_mutex_lock(&s->lock);
    s->queue_end_us = 0;
    pthread_mutex_unlock(&s->lock);
    static const char *const modes[] = { "paced", "not paced", "refused", "a two-block queue", "a two-block queue" };
    snprintf(why, why_len, "simulated output (PC test), route %d, %s", route, modes[s->out_mode >= 0 && s->out_mode <= 4 ? s->out_mode : 0]);
    return SIM_HANDLE;
}

/* What the simulated device "hears": left equals right, and whether the block carries the test tone. */
static void inspect(pv_sim *s, const int16_t *stereo) {
    static _Thread_local int16_t left[PV_OUT_GRAIN];
    for (int i = 0; i < PV_OUT_GRAIN; i++) {
        left[i] = stereo[2 * i];
        if (stereo[2 * i] != stereo[2 * i + 1]) s->out_stereo_mismatch = 1;
    }
    int hz = akn_pcm_tone_hz(left, PV_OUT_GRAIN, PV_OUT_RATE);
    if (hz >= 840 && hz <= 920) s->out_tone_blocks++;
}

static int32_t h_out_output(void *u, int32_t handle, const int16_t *stereo) {
    pv_sim *s = u;
    if (handle != SIM_HANDLE) return (int32_t)0x80260003;
    unsigned long calls = ++s->out_calls;
    long fail_after = s->out_fail_after;
    if (fail_after >= 0 && (long)calls > fail_after) return (int32_t)0x80260008;
    if (stereo) inspect(s, stereo);
    uint64_t block = (uint64_t)PV_OUT_GRAIN * 1000000u / PV_OUT_RATE;
    int mode = s->out_mode;
    if (mode == 0) {
        sleep_us(block);
    } else if (mode == 3 || mode == 4) {
        /* the queue plays in real time; a full queue blocks the caller until a block has played */
        pthread_mutex_lock(&s->lock);
        uint64_t now = h_now_us(u);
        if (s->queue_end_us < now) s->queue_end_us = now; /* it ran dry: the device played silence meanwhile */
        s->queue_end_us += block;
        uint64_t room_at = s->queue_end_us - (SIM_QUEUE_BLOCKS - 1) * block;
        pthread_mutex_unlock(&s->lock);
        if (room_at > now) sleep_us(room_at - now);
    }
    return 0;
}

static int32_t h_out_close(void *u, int32_t handle) {
    pv_sim *s = u;
    if (handle != SIM_HANDLE) return (int32_t)0x80260003;
    s->out_closes++;
    return 0;
}

static double g_phase;

static int32_t h_mic_open(void *u, int32_t user, unsigned rate, char *why, size_t why_len) {
    pv_sim *s = u;
    if (s->mic_mode == 3) {
        snprintf(why, why_len, "dlopen(libSceAudioIn.sprx) failed: simulated");
        return PV_NO_LIBRARY;
    }
    s->mic_open_calls++;
    if (rate == 48000) s->mic_open_calls_48k++;
    snprintf(why, why_len, "simulated microphone (PC test), user 0x%08X, %u Hz, %d frames", (unsigned)user, rate, PV_MIC_GRAIN);
    if (s->mic_mode == 2) return (int32_t)0x80260105;
    if (s->mic_mode == 4) return (int32_t)0x809B0081;
    if (s->mic_mode == 5 && rate == 16000) return (int32_t)0x80260103;
    s->mic_opens++;
    g_phase = 0;
    return SIM_MIC_HANDLE;
}

static int32_t h_mic_read(void *u, int32_t handle, int16_t *samples) {
    pv_sim *s = u;
    if (handle != SIM_MIC_HANDLE) return (int32_t)0x80260101;
    unsigned long reads = ++s->mic_reads;
    long fail_after = s->mic_fail_after;
    sleep_us((uint64_t)PV_MIC_GRAIN * 1000000u / 16000u);
    if (fail_after >= 0 && (long)reads > fail_after) return (int32_t)0x80260109;
    for (int i = 0; i < PV_MIC_GRAIN; i++) {
        if (s->mic_mode == 1) {
            samples[i] = 0;
        } else {
            samples[i] = (int16_t)lrint(9000.0 * sin(g_phase));
            g_phase += 2.0 * 3.14159265358979323846 * (double)s->mic_tone_hz / 16000.0;
        }
    }
    return 0;
}

static int32_t h_mic_close(void *u, int32_t handle) {
    pv_sim *s = u;
    if (handle != SIM_MIC_HANDLE) return (int32_t)0x80260101;
    s->mic_closes++;
    return 0;
}

static long after(const char *v, const char *prefix) {
    size_t n = strlen(prefix);
    return v && strncmp(v, prefix, n) == 0 ? strtol(v + n, NULL, 10) : -1;
}

void pv_sim_from_env(pv_sim *sim) {
    memset(sim, 0, sizeof(*sim));
    pthread_mutex_init(&sim->lock, NULL);
    sim->mic_tone_hz = 300;
    sim->mic_fail_after = -1;
    sim->out_fail_after = -1;
    const char *mic = getenv("PV_SIM_MIC");
    if (mic && strncmp(mic, "tone:", 5) == 0) sim->mic_tone_hz = atoi(mic + 5) > 0 ? atoi(mic + 5) : 300;
    else if (mic && strcmp(mic, "silence") == 0) sim->mic_mode = 1;
    else if (mic && strcmp(mic, "refused") == 0) sim->mic_mode = 2;
    else if (mic && strcmp(mic, "missing") == 0) sim->mic_mode = 3;
    else if (mic && strcmp(mic, "process-refused") == 0) sim->mic_mode = 4;
    else if (mic && strcmp(mic, "rate-refused") == 0) sim->mic_mode = 5;
    sim->mic_fail_after = after(mic, "fail-after:");
    const char *out = getenv("PV_SIM_OUT");
    if (out && strcmp(out, "unpaced") == 0) sim->out_mode = 1;
    else if (out && strcmp(out, "refused") == 0) sim->out_mode = 2;
    else if (out && strcmp(out, "queue") == 0) sim->out_mode = 3;
    else if (out && strcmp(out, "user-refused") == 0) sim->out_mode = 4;
    sim->out_fail_after = after(out, "fail-after:");
    const char *ctx = getenv("PV_SIM_CONTEXT");
    if (ctx && strcmp(ctx, "foreground") == 0) sim->context = 1;
    else if (ctx && strcmp(ctx, "unknown") == 0) sim->context = 2;
    const char *file = getenv("PV_SIM_BIGAPP_FILE");
    if (file) snprintf(sim->bigapp_file, sizeof(sim->bigapp_file), "%s", file);
}

void pv_backend_host(pv_backend *be, pv_sim *sim) {
    pv_backend b = { "PC (simulated console)", sim, h_now_us, h_notify, h_bigapp, h_describe, h_process, h_foreground_user, h_out_init,
                     h_out_open, h_out_output, h_out_close, h_mic_open, h_mic_read, h_mic_close };
    *be = b;
}
