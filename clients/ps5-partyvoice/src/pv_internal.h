/* AKENO Party Voice: the engine's internals, shared by pv_engine.c, pv_net.c and pv_audio.c. */
#ifndef PV_INTERNAL_H
#define PV_INTERNAL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "jitter.h"
#include "pv.h"
#include "pv_engine.h"
#include "voice_codec.h"
#include "voice_dsp.h"

#define PV_TX_QUEUE 12          /* 240 ms of microphone packets waiting for the network */
#define PV_THREAD_STACK (1024 * 1024) /* libopus keeps its work buffers on the stack (VAR_ARRAYS); TLS needs room too */
#define PV_NOTES 8
#define PV_REFUSALS 8

/* A console refusal, remembered so the same call is not repeated in the same situation. */
typedef struct {
    int what;          /* 0 the microphone, 1..PV_ROUTES an output route */
    int32_t user;      /* the user the call was made for (0xFF: the system user) */
    int32_t bigapp;    /* the running app when it was refused (<= 0: none) */
    int32_t rc;
} pv_refusal;

typedef struct {
    uint8_t data[AKN_VOICE_MAX_PACKET + 3];
    size_t len;
    int nonzero;                /* the frame it came from had sound in it (not exact zeros) */
} pv_tx_packet;

/* One party voice session (join to leave). Its threads stop when run is 0. */
typedef struct pv_session {
    pv_engine *e;
    atomic_int run;
    char party_id[64];
    pthread_t net, play, cap;
    int have_net, have_play, have_cap;
    atomic_int net_done, play_done, cap_done;
    atomic_int cap_run;         /* the capture thread runs while this is 1 */
    int32_t cap_user;           /* the foreground user the microphone is opened for */
    /* lock: what the threads exchange */
    pthread_mutex_t lock;
    akn_jitter jitter;
    pv_tx_packet tx[PV_TX_QUEUE];
    int tx_head, tx_count;
    int connected;
} pv_session;

struct pv_engine {
    pv_config cfg;
    const pv_backend *be;
    pthread_mutex_t lock;       /* st, the controls, session pointer, notes */
    pv_status st;
    pv_session *session;
    pv_session *closing[4];     /* sessions whose threads are finishing */
    int closing_count;
    /* the test sound: played by the session's playback thread, or by its own thread */
    int test_pending;           /* beeps still to mix in (route number = beep count) */
    pthread_t test_thread;
    int have_test;
    atomic_int test_done;
    /* main loop */
    uint64_t started_us, last_tick_us, last_game_poll_us, last_report_us, last_connected_tick_us;
    int report_dirty;
    int out_initialized;
    pv_refusal refusals[PV_REFUSALS];
    int refusal_count;
    char notes[PV_NOTES][200];
    int note_count;
    uint64_t last_people_note_us;
    char known_people[AKN_VOICE_MAX_PARTICIPANTS][64];
    int known_count;
    int people_noted;
    pthread_mutex_t log_lock;
};

uint64_t pv_now_ms(const pv_engine *e);
/* A line in voice.log and on stdout: states, codes and counts only. */
void pv_log(pv_engine *e, const char *event, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* A system notification, sent later by the main loop (never from an audio thread). Needs e->lock. */
void pv_note_locked(pv_engine *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void pv_set_failure_locked(pv_engine *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void *pv_net_thread(void *arg);
void *pv_play_thread(void *arg);
void *pv_cap_thread(void *arg);
void *pv_test_thread(void *arg);
int pv_start_thread(pthread_t *thread, void *(*fn)(void *), void *arg);
/* Sleeps up to ms while the session runs. */
void pv_sleep_while(const atomic_int *run, unsigned ms);
void pv_sleep_ms(unsigned ms);

/* 1: the console should be heard by the others right now (microphone on, open, not muted). */
int pv_transmitting_locked(const pv_engine *e);

/* The refusal of this call in this situation (same device, user and running app), or NULL. */
const pv_refusal *pv_refusal_find_locked(const pv_engine *e, int what, int32_t user);
void pv_refusal_remember_locked(pv_engine *e, int what, int32_t user, int32_t rc);
/* This process's context (read only), into e->st (e->lock not held). */
void pv_refresh_process(pv_engine *e);

#endif
