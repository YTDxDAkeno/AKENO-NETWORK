/*
 * AKENO Audio Lab 3.1 (EXPERIMENTAL) - a standalone, isolated PS5 audio-output experiment.
 *
 * It is NOT the AKENO NETWORK console app and NOT Audio Lab v3 inside it. It reuses the proven,
 * hardware-relevant code of Audio Lab v3 verbatim (audio_lab_pcm.*, audio_lab_state.*, commit
 * 05ad4f3) and wraps it in a tiny, automatic, network-free payload: one instance (flock), plain
 * reports, clean shutdown.
 *
 * Isolation (enforced by console/check-audiolab.sh and by having no network code at all):
 *   - uses ONLY /data/akeno-audiolab/; never /data/akeno;
 *   - opens no socket, serves no page, uses port 8642 for nothing, never reaches a server/LiveKit;
 *   - announces, waits five seconds, then plays exactly one default tone;
 *   - opens the microphone NEVER; calls no focus/attach/routing/volume/HDMI function;
 *   - sceAudioOutGetPortState is disabled and is not resolved or called.
 *
 * The report records only this payload's own API results. It never infers audibility.
 */
#ifndef AKENO_AL_H
#define AKENO_AL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "al_backend.h"
#include "audio_lab_pcm.h"
#include "audio_lab_state.h"

#define AL_VERSION "3.1.0"
#ifndef AL_BUILD_ID
#define AL_BUILD_ID "dev"
#endif

#define AL_OUT_DIR "/data/akeno-audiolab"

/* Tone defaults (identical to Audio Lab v3): 440 Hz, -20 dBFS, 250 ms silence + 1.5 s + 250 ms. */
#define AL_TONE_FREQ_HZ 440u
#define AL_TONE_LEAD_MS 250u
#define AL_TONE_MS 1500u
#define AL_TONE_TAIL_MS 250u
#define AL_ONESHOT_DELAY_MS 5000u

#define AL_RUNS_KEPT 16          /* reports keep the last N runs */
#define AL_RUNS_PER_LAUNCH 1     /* defence in depth: the automatic payload may run only once */

/* One tone preset / level: the exact set of Audio Lab v3. */
typedef struct {
    const char *id, *label;
    int format;                  /* AUDIO_LAB_FORMAT_* */
    unsigned grain;
} al_preset;

typedef struct {
    const char *id, *label;
    double amplitude;
} al_level;

const al_preset *al_presets(size_t *count);
const al_level *al_levels(size_t *count);
const al_preset *al_find_preset(const char *id); /* NULL unknown; default is presets[0] for "" */
const al_level *al_find_level(const char *id);   /* NULL unknown; default is the moderate level */

/* How a tone run ended (classification identical in spirit to v3). */
typedef enum {
    AL_OUT_NOT_RUN = 0,
    AL_OUT_NOT_SUPPORTED,        /* libSceAudioOut or its functions unavailable */
    AL_OUT_INIT_FAILED,
    AL_OUT_OPEN_FAILED,
    AL_OUT_SUBMIT_FAILED,
    AL_OUT_CANCELLED,
    AL_OUT_ACCEPTED              /* every call succeeded; audibility is the owner's report only */
} al_outcome;

const char *al_outcome_name(al_outcome o);

/* The owner's own verdict for a run (never inferred). */
typedef enum { AL_HEARD_UNREPORTED = -1, AL_HEARD_NO = 0, AL_HEARD_YES = 1 } al_heard;

typedef struct {
    int id;
    long long started_unix;
    const al_preset *preset;
    const al_level *level;
    unsigned long frames_planned;
    int rc;                      /* al_audio_run return */
    al_audio_result result;      /* raw measurements (al_backend.h) */
    al_outcome outcome;
    int paced;                   /* 1 consumed at playback speed, 0 faster, -1 too few calls */
    al_heard heard;
    char route[24];              /* where it was heard, owner-reported */
} al_run;

/* Outcome classification and pacing verdict for a finished run (unit-tested). */
al_outcome al_classify(int rc, const al_audio_result *r);
int al_paced(const al_audio_result *r, unsigned grain);

/* ---- the run state ------------------------------------------------------------------------- */

typedef struct {
    char build_id[32];
    char firmware[16];
    int have_fw;
    uint32_t firmware_raw;
    int pid;

    int port_state_opt_in;       /* always 0: inspection remains disabled */
    int port_state_supported;    /* always 0: the function is never resolved */

    al_run runs[AL_RUNS_KEPT];
    int next_run;                /* 1-based id of the next run */
    int tone_commands;           /* how many audio runs started this launch */
    unsigned tones_played;
    long long started_unix;
    long long last_heartbeat_unix;
    int exited;                  /* automatic one-shot processing finished */
    char last_note[160];         /* completion or error note */

    /* Protects every field above that the tone worker and the main (report) thread both touch:
     * runs[], next_run, tone_commands, tones_played, port_state_supported. The engine holds it only
     * for the short prepare/commit sections, never during the blocking audio call; report writers
     * hold it while they read. Initialised by al_engine_init. */
    pthread_mutex_t lock;
    int lock_ready;
} al_state;

al_run *al_run_by_id(al_state *st, int id);
void al_state_lock(al_state *st);
void al_state_unlock(al_state *st);

/* ---- engine (al_engine.c) ------------------------------------------------------------------ */

void al_engine_init(al_state *st, const al_backend *be);
int al_engine_run_tone(al_state *st, const al_backend *be, const al_preset *preset, const al_level *level,
                       atomic_int *cancel);
int al_engine_run_one_shot(al_state *st, const al_backend *be, atomic_int *cancel);
int al_engine_feedback(al_state *st, int run_id, al_heard heard, const char *route);

/* ---- reports ------------------------------------------------------------------------------- */

size_t al_report_json(const al_state *st, char *out, size_t len);
size_t al_report_txt(const al_state *st, char *out, size_t len);   /* German owner summary + detail */
size_t al_report_status_json(const al_state *st, const char *state, long long now_unix, char *out, size_t len);

#endif
