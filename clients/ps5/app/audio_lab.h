/*
 * Audio lab v2: experimental PS5 audio research (docs/PS5_AUDIO_RESEARCH.md). Compiled only into
 * the separate audio lab build (AKENO_EXPERIMENTAL_AUDIO); the release build of the console app
 * contains none of it. Every action is started by the user on the diagnostics page; nothing runs
 * on its own and no audio is captured, stored or sent anywhere.
 *
 * Output test states (never "audio works" without the user's own report):
 *   not-run        no test yet
 *   not-supported  libSceAudioOut.sprx or its functions are not available
 *   init-failed    sceAudioOutInit failed
 *   open-failed    sceAudioOutOpen returned no port
 *   submit-failed  sceAudioOutOutput returned an error
 *   accepted       every call succeeded: the system took the samples (audibility unknown)
 *   cancelled      stopped by the user before the end
 * and per run the user's report: heard (where) or not heard.
 */
#ifndef AKENO_APP_AUDIO_LAB_H
#define AKENO_APP_AUDIO_LAB_H

#include "buf.h"

/* Called once at start-up: where the plain-text report is written, and what it describes. */
void audio_lab_init(const char *report_path, const char *build, const char *firmware);

/* Read-only probe of the audio libraries (names are looked up, never called). */
void audio_lab_probe(void);

/*
 * Starts one output test on a worker thread. preset/level are ids from the status JSON, or NULL
 * for the defaults; delay_seconds is 0 or 10 (to leave the browser first). Returns 0 when started,
 * 1 busy, 2 too soon after the last test, 3 test limit for this launch reached, -1 invalid input,
 * -2 the thread could not be started (err says why).
 */
int audio_lab_tone_start(const char *preset, const char *level, int delay_seconds, char *err, size_t err_len);

/* Cancels a waiting or playing test. Returns 0 when one was running. */
int audio_lab_cancel(void);

/* The user's report for a run: heard (1) or not (0), and where ("tv", "controller-headset",
 * "usb-headset", "other" or ""). Returns 0, or -1 for an unknown run. */
int audio_lab_feedback(int run_id, int heard, const char *route);

/* Result of the page's own Web Audio test in the system browser (for comparison). */
typedef struct {
    int api_available;     /* AudioContext or webkitAudioContext exists */
    char state[16];        /* context state after resume(): running, suspended, closed, error */
    int sample_rate;
    int clock_advanced;    /* currentTime moved forward while the tone played */
    int heard;             /* 1 heard, 0 not heard, -1 not reported */
    char route[24];
    char error[120];
} audio_lab_browser_result;
void audio_lab_browser_report(const audio_lab_browser_result *result);

/* {"experimental":true,"version":2,"state":...,"probe":{...}|null,"runs":[...],...} */
void audio_lab_status_json(akeno_buf *b);

/* Plain-text report without any account data, tokens or credentials. */
void audio_lab_report_text(akeno_buf *b);

/* Stops a test and waits for it (called by the main thread before the app exits). */
void audio_lab_shutdown(void);

#endif
