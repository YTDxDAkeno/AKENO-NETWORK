/*
 * AKENO Audio Lab 3.1 (EXPERIMENTAL) - a standalone, isolated PS5 audio-output experiment.
 *
 * It is NOT the AKENO NETWORK console app and NOT Audio Lab v3 inside it. It reuses the proven,
 * hardware-relevant code of Audio Lab v3 verbatim (audio_lab_pcm.*, audio_lab_state.*, commit
 * 05ad4f3) and wraps it in a tiny, file-controlled, network-free payload modelled on the System
 * Explorer: one instance (flock), a control file for commands, plain reports, clean shutdown.
 *
 * Isolation (enforced by console/check-audiolab.sh and by having no network code at all):
 *   - uses ONLY /data/akeno-audiolab/; never /data/akeno;
 *   - opens no socket, serves no page, uses port 8642 for nothing, never reaches a server/LiveKit;
 *   - plays a tone ONLY after the owner writes a `tone` command (no automatic playback);
 *   - opens the microphone NEVER; calls no focus/attach/routing/volume/HDMI function;
 *   - sceAudioOutGetPortState is OFF unless the owner opts in with a file, and even then it is a
 *     read-only query on THIS test's own port only.
 *
 * Why this is not just a repeat of v1/v2 (which were accepted-but-silent): the plain tone from a
 * background payload IS a known result and the report says so. The one NEW measurement is the
 * opt-in port-state read of the test's own port, which can tell apart "the system attached the
 * port to no output device" from "attached to a device at nonzero volume yet still silent" - IF
 * the unverified field layout holds. The report never claims more than the evidence supports.
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

#define AL_RUNS_KEPT 16          /* reports keep the last N runs */
#define AL_RUNS_PER_LAUNCH 30    /* hard cap on tone commands per process */
#define AL_COOLDOWN_MS 3000      /* minimum gap between tone commands */
#define AL_SESSION_MAX_S 1800u   /* the control loop exits by itself after this long */
#define AL_CONTROL_POLL_MS 1000u /* how often the control file is read */
#define AL_HEARTBEAT_S 10u       /* status.json heartbeat */

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

/* Decision the control loop makes for one command line (unit-tested). */
typedef enum {
    AL_CMD_NONE = 0,             /* blank/comment/unknown: ignored, noted */
    AL_CMD_STATUS,               /* rewrite the reports now */
    AL_CMD_TONE,                 /* play one tone (args: preset, level) */
    AL_CMD_STOP,                 /* cancel a playing/pending tone */
    AL_CMD_EXIT                  /* end the session cleanly */
} al_cmd_kind;

typedef struct {
    al_cmd_kind kind;
    char preset[32];
    char level[32];
} al_cmd;

/* Parses one control line into a command (unit-tested). Returns the kind. */
al_cmd_kind al_parse_command(const char *line, al_cmd *out);

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

    int port_state_opt_in;       /* the enable-portstate file exists: the opt-in probe may run */
    int port_state_supported;    /* the function was resolved at run time */

    al_run runs[AL_RUNS_KEPT];
    int next_run;                /* 1-based id of the next run */
    int tone_commands;           /* how many tone commands accepted this launch */
    unsigned commands_seen;      /* every non-blank control line */
    unsigned tones_played;
    unsigned stops;
    long long started_unix;
    long long last_heartbeat_unix;
    int exited;                  /* an exit command ended the loop */
    char last_note[160];         /* the most recent human note (busy, cooldown, limit, ...) */

    /* Protects every field above that the tone worker and the main (report) thread both touch:
     * runs[], next_run, tone_commands, tones_played, port_state_supported. The worker holds it only
     * for the short prepare/commit sections, never during the blocking audio call; the report
     * writers and the main loop's reads hold it while they read. Initialised by al_engine_init. */
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
int al_engine_feedback(al_state *st, int run_id, al_heard heard, const char *route);

/* ---- reports ------------------------------------------------------------------------------- */

size_t al_report_json(const al_state *st, char *out, size_t len);
size_t al_report_txt(const al_state *st, char *out, size_t len);   /* German owner summary + detail */
size_t al_report_status_json(const al_state *st, const char *state, long long now_unix, char *out, size_t len);

#endif
