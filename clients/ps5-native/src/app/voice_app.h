/*
 * Party voice in AKENO Native (docs/PS5_VOICE.md): the threads that move audio between the
 * microphone, the AKENO voice gateway and the console's audio output, and the local microphone
 * test. The state the screens show is app->voice (core/voice.h), guarded by app->lock.
 *
 *   network   voice pass from the AKENO server (through the daemon, IPC 3), TLS WebSocket to the
 *             gateway, hello, control messages, received Opus into the jitter buffer, queued
 *             Opus out; reconnects with a new pass after a loss
 *   playback  jitter buffer -> Opus decode/concealment -> volume -> the console's audio output
 *             (sceAudioOut, the path Native 0.1 proved audible on 12.20)
 *   capture   microphone (sceAudioIn, loaded only now) -> 48 kHz -> gain -> Opus -> network
 *
 * Nothing is recorded or stored; the microphone is opened only after the user confirmed, and
 * closed when voice or the test ends, the party is left, or the app exits.
 */
#ifndef AKN_VOICE_APP_H
#define AKN_VOICE_APP_H

#include "app.h"

/* Starts voice in a party (the user has confirmed the microphone). 0, or -1 with a toast text. */
int akn_voice_join(akn_app *app, const char *party_id, const char *party_name, char *why, size_t why_len);
/* Ends voice or the microphone test; the threads finish in the background (akn_voice_poll). */
void akn_voice_leave(akn_app *app, const char *reason);
void akn_voice_toggle_mute(akn_app *app);
void akn_voice_toggle_deafen(akn_app *app);
/* which: 0 output volume, 1 microphone volume; dir -1 or +1. */
void akn_voice_adjust(akn_app *app, int which, int dir);
/* The local microphone test: level meter, optional playback to the user; nothing is sent. */
int akn_voice_mic_test(akn_app *app, char *why, size_t why_len);
void akn_voice_toggle_monitor(akn_app *app);
/* Whether voice or the test runs (or is still shutting down). */
int akn_voice_busy(akn_app *app);
/* Main loop: frees a finished session. */
void akn_voice_poll(akn_app *app);
/* Exit: stops everything and waits (bounded). */
void akn_voice_shutdown(akn_app *app);

#endif
