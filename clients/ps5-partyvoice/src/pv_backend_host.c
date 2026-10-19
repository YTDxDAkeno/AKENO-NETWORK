/*
 * A simulated console for the PC tests of AKENO Party Voice. Nothing here is evidence about a PS5.
 *
 *   PV_SIM_MIC   tone:<hz> (default tone:300) | silence | refused | missing | fail-after:<reads>
 *   PV_SIM_OUT   paced (default) | unpaced | refused | fail-after:<calls>
 *   PV_SIM_BIGAPP_FILE  a file whose content is the running app ID (written by the test to "start a game")
 *
 * The microphone delivers a 16 kHz tone in real time (each read blocks for one grain, like the
 * console's); the output consumes blocks at playback speed unless told otherwise.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pv_backend.h"

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

static int32_t h_out_init(void *u) {
    (void)u;
    return 0;
}

static int32_t h_out_open(void *u, int route, char *why, size_t why_len) {
    pv_sim *s = u;
    if (!pv_route_get(route)) return (int32_t)0x80260001;
    if (s->out_mode == 2) {
        snprintf(why, why_len, "simulated output refused");
        return (int32_t)0x8026000A;
    }
    s->out_opens++;
    snprintf(why, why_len, "simulated output (PC test), route %d, %s", route, s->out_mode == 1 ? "not paced" : "paced");
    return 7;
}

static int32_t h_out_output(void *u, int32_t handle, const int16_t *stereo) {
    pv_sim *s = u;
    (void)stereo;
    if (handle != 7) return (int32_t)0x80260003;
    unsigned long calls = ++s->out_calls;
    long fail_after = s->out_fail_after;
    if (fail_after >= 0 && (long)calls > fail_after) return (int32_t)0x80260008;
    if (s->out_mode == 0) sleep_us((uint64_t)PV_OUT_GRAIN * 1000000u / PV_OUT_RATE);
    return 0;
}

static int32_t h_out_close(void *u, int32_t handle) {
    pv_sim *s = u;
    if (handle != 7) return (int32_t)0x80260003;
    s->out_closes++;
    return 0;
}

static double g_phase;

static int32_t h_mic_open(void *u, unsigned *rate, char *why, size_t why_len) {
    pv_sim *s = u;
    if (s->mic_mode == 3) {
        snprintf(why, why_len, "dlopen(libSceAudioIn.sprx) failed: simulated");
        return (int32_t)0x80000002;
    }
    if (s->mic_mode == 2) {
        snprintf(why, why_len, "sceAudioInOpen refused 16000 and 48000 Hz (simulated)");
        return (int32_t)0x80260105;
    }
    s->mic_opens++;
    *rate = 16000;
    g_phase = 0;
    snprintf(why, why_len, "simulated microphone (PC test), 16000 Hz, %d frames", PV_MIC_GRAIN);
    return 3;
}

static int32_t h_mic_read(void *u, int32_t handle, int16_t *samples) {
    pv_sim *s = u;
    if (handle != 3) return (int32_t)0x80260103;
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
    if (handle != 3) return (int32_t)0x80260103;
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
    sim->mic_fail_after = after(mic, "fail-after:");
    const char *out = getenv("PV_SIM_OUT");
    if (out && strcmp(out, "unpaced") == 0) sim->out_mode = 1;
    else if (out && strcmp(out, "refused") == 0) sim->out_mode = 2;
    sim->out_fail_after = after(out, "fail-after:");
    const char *file = getenv("PV_SIM_BIGAPP_FILE");
    if (file) snprintf(sim->bigapp_file, sizeof(sim->bigapp_file), "%s", file);
}

void pv_backend_host(pv_backend *be, pv_sim *sim) {
    pv_backend b = { "PC (simulated console)", sim, h_now_us, h_notify, h_bigapp, h_describe, h_out_init, h_out_open, h_out_output,
                     h_out_close, h_mic_open, h_mic_read, h_mic_close };
    *be = b;
}
