/*
 * Everything AKENO Party Voice needs from the console: the PS5 implementation (pv_backend_ps5.c,
 * the only file that declares or calls console functions) and a simulated console for the PC
 * tests (pv_backend_host.c). The engine never assumes that a successful call means audible sound
 * or a working microphone: it measures what the calls return and reports it.
 */
#ifndef PV_BACKEND_H
#define PV_BACKEND_H

#include <stddef.h>
#include <stdint.h>

/* Output routes: the Voice Lab's four bounded variants (docs/PS5_BACKGROUND_VOICE.md, test B). */
typedef struct {
    int foreground_user; /* 0: the system user (0xFF), 1: the foreground user */
    int port_type;       /* 0 main; 2 voice, 3 personal (PS4 numbering, unverified on the PS5) */
    const char *label;
} pv_route;

const pv_route *pv_route_get(int route); /* 1..PV_ROUTES; NULL otherwise */

#define PV_OUT_RATE 48000
#define PV_OUT_GRAIN 1024 /* frames per output call: the default Native 0.1 proved audible in the foreground */
#define PV_MIC_GRAIN 256  /* frames per microphone read */

typedef struct {
    const char *name; /* "PS5" or "PC (simulated console)" */
    void *user;
    uint64_t (*now_us)(void *u);
    /* A system notification (PS5) or a line on stderr (PC). 0 on success. */
    int (*notify)(void *u, const char *text);
    /* The running game or application (> 0), else <= 0. Read only. */
    int32_t (*bigapp)(void *u);
    /* Firmware and process, for the report. */
    void (*describe)(void *u, char *out, size_t len);

    /* Audio output, S16 stereo at 48 kHz, PV_OUT_GRAIN frames per call. */
    int32_t (*out_init)(void *u);
    int32_t (*out_open)(void *u, int route, char *why, size_t why_len); /* handle >= 0 or the system code */
    int32_t (*out_output)(void *u, int32_t handle, const int16_t *stereo);
    int32_t (*out_close)(void *u, int32_t handle);

    /* Microphone, S16 mono, PV_MIC_GRAIN frames per read; loaded on first use. */
    int32_t (*mic_open)(void *u, unsigned *rate, char *why, size_t why_len);
    int32_t (*mic_read)(void *u, int32_t handle, int16_t *samples);
    int32_t (*mic_close)(void *u, int32_t handle);
} pv_backend;

/* Process CPU time in milliseconds, or -1 when the clock is not available. */
long long pv_cpu_ms(void);

#ifdef PV_PS5
void pv_backend_ps5(pv_backend *be);
#else
#include <pthread.h>
#include <stdatomic.h>

/* PC simulation, configured from the environment (PV_SIM_MIC, PV_SIM_OUT, PV_SIM_BIGAPP_FILE). */
typedef struct {
    /* atomics: the tests change the modes while the audio threads run */
    atomic_int mic_mode;       /* 0 tone, 1 silence (exact zeros), 2 open fails, 3 library missing */
    int mic_tone_hz;
    atomic_long mic_fail_after;  /* reads before every read fails (< 0: never) */
    atomic_int out_mode;       /* 0 paced, 1 not paced (returns at once), 2 open fails */
    atomic_long out_fail_after;  /* output calls before every call fails (< 0: never) */
    char bigapp_file[256];     /* its content is the running app ID (a game "starts" when it changes) */
    /* what happened (tests) */
    atomic_ulong notifications, mic_reads, mic_opens, mic_closes, out_calls, out_opens, out_closes;
    pthread_mutex_t lock;
    char last_notification[256];
} pv_sim;

void pv_sim_from_env(pv_sim *sim);
void pv_backend_host(pv_backend *be, pv_sim *sim);
#endif

#endif
