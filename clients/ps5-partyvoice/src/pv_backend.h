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

#include "context.h"

/* Output routes. Route 1 is the call AKENO Native makes as the foreground app (heard on 12.20).
 * Route 2 is the one control that changes a single variable (the user). Routes 3 and 4 (PS4 port
 * types 2 and 3) are retired: on 12.20 they were refused exactly like route 2 (0x80260011), and
 * what those numbers mean on a PS5 is not known, so they are never opened again. */
typedef struct {
    int foreground_user; /* 0: the system user (0xFF), 1: the foreground user */
    int port_type;       /* 0 main */
    int retired;         /* 1: refused without a console call */
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
    /* What kind of process this is (read only): this process's app info and the foreground app,
     * decoded and classified by AKENO Native's context.c (foreground application or background). */
    void (*process)(void *u, akn_context_raw *raw);
    /* The foreground user (read only). 0 on success. */
    int32_t (*foreground_user)(void *u, int32_t *user);

    /* Audio output, S16 stereo at 48 kHz, PV_OUT_GRAIN frames per call. */
    int32_t (*out_init)(void *u);
    int32_t (*out_open)(void *u, int route, char *why, size_t why_len); /* handle >= 0 or the system code */
    int32_t (*out_output)(void *u, int32_t handle, const int16_t *stereo);
    int32_t (*out_close)(void *u, int32_t handle);

    /* Microphone, S16 mono, PV_MIC_GRAIN frames per read; the library is loaded on first use.
     * mic_open tries exactly the one configuration it is given: what to try, and when not to try
     * again, is the engine's decision (pv_audio.c), so it is the same on the PC and the PS5. */
    int32_t (*mic_open)(void *u, int32_t user, unsigned rate, char *why, size_t why_len);
    int32_t (*mic_read)(void *u, int32_t handle, int16_t *samples);
    int32_t (*mic_close)(void *u, int32_t handle);
} pv_backend;

/* Codes the backends return before any console call. */
#define PV_NO_USER_SERVICE ((int32_t)0x80000001)
#define PV_NO_LIBRARY ((int32_t)0x80000002)

/* Process CPU time in milliseconds, or -1 when the clock is not available. */
long long pv_cpu_ms(void);

#ifdef PV_PS5
void pv_backend_ps5(pv_backend *be);
#else
#include <pthread.h>
#include <stdatomic.h>

/* PC simulation, configured from the environment (pv_backend_host.c). */
typedef struct {
    /* atomics: the tests change the modes while the audio threads run */
    atomic_int mic_mode;       /* 0 tone, 1 silence, 2 refused (argument), 3 library missing, 4 refused for the process
                                  (0x809B0081, as the PS5 answered the background service), 5 16 kHz refused (rate) */
    int mic_tone_hz;
    atomic_long mic_fail_after;  /* reads before every read fails (< 0: never) */
    atomic_int out_mode;       /* 0 paced (each call blocks one block), 1 not paced (returns at once), 2 open fails,
                                  3 queue (a device clock drains a two-block queue, like a console port),
                                  4 the foreground user's ports are refused (0x80260011), the system user's is a queue */
    atomic_long out_fail_after;  /* output calls before every call fails (< 0: never) */
    atomic_int context;        /* 0 background payload (default), 1 foreground application, 2 unknown */
    char bigapp_file[256];     /* its content is the running app ID (a game "starts" when it changes) */
    /* what happened (tests) */
    atomic_ulong notifications, mic_reads, mic_opens, mic_open_calls, mic_open_calls_48k, mic_closes, out_calls, out_opens,
        out_open_calls, out_closes;
    atomic_ulong out_tone_blocks;  /* submitted blocks whose left channel carries the 880 Hz test tone */
    atomic_int out_stereo_mismatch; /* a submitted block whose left and right channels differ */
    pthread_mutex_t lock;
    char last_notification[256];
    uint64_t queue_end_us;     /* out_mode 3/4: when the queued audio has played */
} pv_sim;

void pv_sim_from_env(pv_sim *sim);
void pv_backend_host(pv_backend *be, pv_sim *sim);
#endif

#endif
