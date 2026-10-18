#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "backend.h"

static uint64_t h_now_us(void *u) {
    (void)u;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void h_sleep_ms(void *u, unsigned ms) {
    (void)u;
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int h_notify(void *u, const char *text) {
    akn_lab_sim *s = u;
    s->notifications++;
    snprintf(s->last_notification, sizeof(s->last_notification), "%s", text);
    return 0;
}

static int32_t h_bigapp(void *u) {
    akn_lab_sim *s = u;
    s->bigapp_polls++;
    if (s->game_after_polls >= 0 && s->bigapp_polls > (unsigned long)s->game_after_polls) return 0x60000123;
    return 0;
}

static int32_t h_user(void *u, int32_t *user) {
    akn_lab_sim *s = u;
    if (s->no_foreground_user) return (int32_t)0x80960009;
    *user = 0x10000001;
    return 0;
}

static void h_describe(void *u, char *out, size_t len) {
    (void)u;
    snprintf(out, len, "PC simulation (not a PS5)");
}

static int32_t h_out_init(void *u) {
    (void)u;
    return 0;
}

static int32_t h_out_open(void *u, int32_t user, int32_t type, unsigned grain) {
    akn_lab_sim *s = u;
    (void)user;
    (void)grain;
    if (type >= 0 && type < 31 && (s->fail_port_types & (1 << type))) return (int32_t)0x8026000A;
    return 7;
}

static int32_t h_out_output(void *u, int32_t handle, const int16_t *stereo) {
    akn_lab_sim *s = u;
    (void)handle;
    if (stereo && s->pace) h_sleep_ms(u, 21);
    return 0;
}

static int32_t h_out_close(void *u, int32_t handle) {
    (void)u;
    (void)handle;
    return 0;
}

static int32_t h_mic_open(void *u, int32_t user, char *why, size_t why_len) {
    akn_lab_sim *s = u;
    (void)user;
    if (s->mic_mode == 2) {
        snprintf(why, why_len, "simulated refusal");
        return (int32_t)0x80260105;
    }
    snprintf(why, why_len, "simulated microphone");
    return 3;
}

static unsigned long g_phase;

static int32_t h_mic_read(void *u, int32_t handle, int16_t *samples) {
    akn_lab_sim *s = u;
    (void)handle;
    for (int i = 0; i < 256; i++) {
        double v = 0;
        if (s->mic_mode == 0) v = 6000.0 * sin(2 * 3.14159265 * 300.0 * (double)(g_phase++) / 16000.0);
        if (s->mic_mode == 3) v = 12;
        samples[i] = (int16_t)v;
    }
    if (s->pace) h_sleep_ms(u, 16);
    return 0;
}

static int32_t h_mic_close(void *u, int32_t handle) {
    (void)u;
    (void)handle;
    return 0;
}

static int h_probe_daemon(void *u, int *ms) {
    (void)u;
    *ms = 1;
    return 0;
}

static int h_probe_server(void *u, int *ms, char *host, size_t host_len) {
    (void)u;
    *ms = 20;
    snprintf(host, host_len, "akeno.example");
    return 0;
}

void akn_lab_backend_host(akn_lab_backend *be, akn_lab_sim *sim) {
    akn_lab_backend b = { "PC simulation", h_now_us, h_sleep_ms, h_notify, h_bigapp, h_user, h_describe,
                          h_out_init, h_out_open, h_out_output, h_out_close, h_mic_open, h_mic_read, h_mic_close,
                          h_probe_daemon, h_probe_server, sim };
    *be = b;
}
