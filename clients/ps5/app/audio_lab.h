/*
 * Audio lab: experimental PS5 voice research (prototypes A and C of docs/PS5_AUDIO_RESEARCH.md).
 * Compiled only into the separate audio lab build (AKENO_EXPERIMENTAL_AUDIO); the release build
 * of the console app contains none of it. Every action is started by the user on the
 * diagnostics page; nothing runs on its own and no audio is captured or stored.
 */
#ifndef AKENO_APP_AUDIO_LAB_H
#define AKENO_APP_AUDIO_LAB_H

#include "buf.h"

/* Prototype A (read-only): runs the probe and keeps its result for audio_lab_status_json(). */
void audio_lab_probe(void);

/*
 * Prototype C: starts the test tone on a worker thread (about one second, quiet). Returns 0 when
 * started, 1 when a tone is already playing, -1 when the thread could not be started.
 */
int audio_lab_tone_start(void);

/* {"experimental":true,"probe":{...}|null,"tone":{...}} */
void audio_lab_status_json(akeno_buf *b);

/* Waits for a playing tone to end (called by the main thread before the app exits). */
void audio_lab_shutdown(void);

#endif
