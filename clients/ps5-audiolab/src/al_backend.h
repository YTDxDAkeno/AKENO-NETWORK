/*
 * The Audio Lab's platform seam. The PS5 implementation (al_backend_ps5.c) is the only file that
 * declares or calls console system functions; the host build (al_backend_host.c) simulates them.
 *
 * The audio-output call sequence and parameters are taken unchanged from the proven Audio Lab v3
 * (clients/ps5/app/platform_ps5.c at commit 05ad4f3): sceAudioOutInit once, sceAudioOutOpen(0xFF,
 * type 0, index 0, grain, 48000, format), sceAudioOutOutput per block (blocks until taken),
 * sceAudioOutClose; every call resolved with dlopen/dlsym. sceAudioOutGetPortState is only
 * resolved and only called when the owner opted in, on the test's own handle, with the guarded
 * arena of audio_lab_state.*.
 */
#ifndef AKENO_AL_BACKEND_H
#define AKENO_AL_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "audio_lab_state.h"

/* Parameters of one tone run (fixed to the verified SDL-driver configuration by the caller). */
typedef struct {
    int32_t user_id;             /* 0xFF, system user */
    int32_t port_type;           /* 0, main port */
    int format;                  /* AUDIO_LAB_FORMAT_* */
    unsigned grain;              /* frames per output call */
    unsigned rate;               /* 48000 */
    unsigned long frames;        /* total frames to submit */
    /* fills `block` with `frames` frames in the run's format; returns nonzero-frame count and peak */
    unsigned long (*fill)(void *user, void *block, unsigned frames, double *peak);
    void *fill_user;
    volatile int *cancel;        /* set to 1 to stop between blocks */
    int measure_port_state;      /* 1 to run the opt-in port-state probe at the three points */
} al_audio_params;

/* Raw measurements of one tone run. Nothing here is a claim about audibility. */
typedef struct {
    int library_loaded;
    int init_called;
    int32_t init_rc;
    int open_called;
    int32_t open_rc;             /* the handle, or a negative code */
    int close_called;
    int32_t close_rc;
    unsigned long calls, calls_ok;
    int32_t last_output_rc;
    int32_t first_error_rc;
    unsigned long first_error_call;
    unsigned long frames_submitted;
    unsigned long nonzero_frames;
    double peak;
    int buffer_aligned;
    int cancelled;
    long long wall_us;           /* first output call start -> last output call end */
    long long call_total_us;     /* summed duration of calls 2..n */
    long long call_min_us, call_max_us;

    int port_state_exported;     /* the function resolved */
    int port_state_opt_in;       /* the owner enabled the probe */
    int port_state_stopped;      /* a guard tripped: no more port-state calls this process */
    audio_lab_state_sample port_state[AUDIO_LAB_STATE_POINTS];

    char error[160];
} al_audio_result;

typedef struct al_backend {
    const char *name;            /* "PlayStation 5" or "PC simulation (not a PS5)" */
    void *user;

    long long (*now_unix)(void *u);
    uint64_t (*now_us)(void *u);
    void (*sleep_ms)(void *u, unsigned ms);
    int (*notify)(void *u, const char *text);

    int (*fw)(void *u, uint32_t *raw);           /* 1 and *raw set, or 0 */
    int (*pid)(void *u);

    /* The output run: the whole open/submit/measure/close sequence (al_backend_ps5.c). */
    int (*audio_run)(void *u, const al_audio_params *p, al_audio_result *r);

    /* Reads bytes appended to the control file since the last call; *reset=1 if it shrank. */
    int (*read_control)(void *u, char *buf, size_t len, int *reset);
    /* 1 when /data/akeno-audiolab/enable-portstate exists. */
    int (*port_state_opt_in)(void *u);
} al_backend;

void al_backend_host(al_backend *be, void *sim);
#ifdef AL_PS5
void al_backend_ps5(al_backend *be);
#endif

#endif
