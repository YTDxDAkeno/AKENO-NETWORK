/*
 * AKENO Voice Lab (EXPERIMENTAL, docs/PS5_BACKGROUND_VOICE.md): measures, on the console, whether a
 * background process can do what party voice during games needs. It runs as a background payload
 * (the same process type as the AKENO daemon), follows a timed plan and tells the player what to
 * do with system notifications:
 *
 *   A  microphone capture in the background (home screen, then while a game runs)
 *   B  audio output in the background (four port variants, each a different number of beeps)
 *   C  full duplex: the microphone played back with a delay while a game runs ("echo")
 *   D  network and scheduling while a game runs (heartbeat gaps, daemon and server reachability)
 *   E  game audio plus AKENO audio together (B and C while a game runs; the player reports)
 *
 * (F, the foreground app's suspend and resume, is measured by AKENO Native itself.)
 *
 * Measurements are numbers only: nothing is recorded or sent; microphone samples are reduced to
 * statistics and, in the echo test, played back once. The report says what was measured and
 * leaves "was it audible" to the player: a successful call is never reported as working sound.
 *
 * Everything that touches the console is behind akn_lab_backend (backend_ps5.c, or
 * backend_host.c for the PC tests).
 */
#ifndef AKN_LAB_H
#define AKN_LAB_H

#include <stddef.h>
#include <stdint.h>

#include "lab_pcm.h"

#define AKN_LAB_VERSION "0.1.0"
#define AKN_LAB_TONES 4

typedef struct {
    const char *name;
    /* Clock and pacing */
    uint64_t (*now_us)(void *u);
    void (*sleep_ms)(void *u, unsigned ms);
    /* Context */
    int (*notify)(void *u, const char *text);
    int32_t (*bigapp_id)(void *u);                 /* the running game or app (>0), else <= 0 */
    int32_t (*foreground_user)(void *u, int32_t *user); /* 0 on success */
    void (*describe)(void *u, char *out, size_t len); /* firmware, pid, process name */
    /* Audio out (S16 stereo, 48 kHz) */
    int32_t (*out_init)(void *u);
    int32_t (*out_open)(void *u, int32_t user, int32_t type, unsigned grain);
    int32_t (*out_output)(void *u, int32_t handle, const int16_t *stereo);
    int32_t (*out_close)(void *u, int32_t handle);
    /* Microphone (S16 mono, 16 kHz, 256 frames per read) */
    int32_t (*mic_open)(void *u, int32_t user, char *why, size_t why_len);
    int32_t (*mic_read)(void *u, int32_t handle, int16_t *samples);
    int32_t (*mic_close)(void *u, int32_t handle);
    /* Network probes: 0 on success, elapsed milliseconds in *ms */
    int (*probe_daemon)(void *u, int *ms);
    int (*probe_server)(void *u, int *ms, char *host, size_t host_len);
    void *user;
} akn_lab_backend;

/* One output variant of test B. */
typedef struct {
    const char *label;
    int foreground_user;   /* 0: system user 0xFF */
    int32_t port_type;     /* 0 main, 2 voice, 3 personal (PS4 numbering; only 0 verified on a PS5) */
} akn_lab_tone_variant;

const akn_lab_tone_variant *akn_lab_tone_variants(void);

typedef struct {
    int32_t open_rc, init_rc;
    unsigned long calls, ok, first_error_call;
    int32_t first_error;
    double avg_ms, max_ms;
    int paced;             /* calls took about one block's duration each */
    int32_t close_rc;
    unsigned beeps;
} akn_lab_tone_result;

typedef struct {
    int32_t user_rc, open_rc;
    char why[160];
    unsigned long reads, read_errors;
    int32_t first_error;
    double read_ms_avg;
    akn_lab_mic_stats stats;
    akn_lab_pcm_verdict verdict;
    int32_t close_rc;
} akn_lab_mic_result;

typedef struct {
    akn_lab_mic_result mic;
    int32_t out_open_rc;
    unsigned long out_calls, out_ok;
    unsigned long long underruns, dropped;
    double out_avg_ms;
} akn_lab_echo_result;

typedef struct {
    /* scheduler: the largest gap between 250 ms heartbeats, per phase */
    double gap_max_ms;
    unsigned long beats;
    unsigned long daemon_ok, daemon_fail, server_ok, server_fail;
    int daemon_ms_max, server_ms_max;
} akn_lab_net_result;

typedef enum { AKN_PHASE_HOME = 0, AKN_PHASE_GAME = 1, AKN_PHASES = 2 } akn_lab_phase;

typedef struct {
    int quick;                 /* PC tests: short waits */
    unsigned wait_game_s;      /* how long to wait for a game (default 300) */
    unsigned settle_s;         /* after a game appears (default 30) */
    int skip_home;             /* start with the game phase (a game is already running) */
    int no_echo;
} akn_lab_options;

typedef struct {
    akn_lab_options opt;
    char context[400];
    char server_host[128];
    int32_t bigapp_start, bigapp_game;
    int game_seen;             /* a game was running for the game phase */
    int ran[AKN_PHASES];
    akn_lab_mic_result mic[AKN_PHASES];
    akn_lab_tone_result tones[AKN_PHASES][AKN_LAB_TONES];
    akn_lab_echo_result echo;
    akn_lab_net_result net[AKN_PHASES];
    int32_t out_init_rc;
    uint64_t started_us, ended_us;
    char log[8192];            /* a timeline of what happened */
    size_t log_len;
} akn_lab_run;

/* Runs the whole plan (blocking, minutes). */
void akn_lab_execute(const akn_lab_backend *be, const akn_lab_options *opt, akn_lab_run *run);

/* The plain-text report (answers to fill in included). Returns the length written. */
size_t akn_lab_report(const akn_lab_run *run, char *out, size_t len);

/* The summary verdict lines only ("A.home.mic=SOUND ..."). */
size_t akn_lab_summary(const akn_lab_run *run, char *out, size_t len);

#endif
