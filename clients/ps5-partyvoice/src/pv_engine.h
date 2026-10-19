/*
 * The voice engine of AKENO Party Voice: one party voice session at a time, its threads, and what
 * they measured. The control interface (pv_control.c) and the report (pv_status.c) read it through
 * pv_engine_status(); nothing else touches the threads.
 *
 *   network   voice pass from the AKENO server through the daemon (IPC 3), TLS WebSocket to the
 *             gateway on the server's own host, hello, mute/deafen, received Opus into the jitter
 *             buffer, queued Opus out; reconnects with a new pass after a loss
 *   playback  jitter buffer -> Opus decode / concealment -> volume -> sceAudioOut (the chosen route);
 *             paced by the output call when it blocks, else by the clock
 *   capture   only after the user turned the microphone on: sceAudioIn -> 48 kHz -> gain -> Opus
 *
 * Every audio boundary is counted: frames read and nonzero, packets encoded and sent, packets
 * received and decoded, output calls and their timing. A successful call is never reported as
 * audible sound or as a microphone that works: the owner's ears and a friend decide that.
 */
#ifndef PV_ENGINE_H
#define PV_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "pv_backend.h"
#include "voice_proto.h"

typedef enum {
    PV_NET_OFF = 0,     /* not in voice */
    PV_NET_REQUESTING,  /* asking the AKENO server for a voice pass (through the daemon) */
    PV_NET_CONNECTING,  /* TLS, WebSocket, hello */
    PV_NET_CONNECTED,   /* the gateway said ready */
    PV_NET_RECONNECTING,
    PV_NET_ENDED        /* stopped by itself: see net_error */
} pv_net_state;

typedef enum {
    PV_MIC_OFF = 0,     /* not turned on by the user (nothing is captured) */
    PV_MIC_OPENING,
    PV_MIC_ACTIVE,
    PV_MIC_UNAVAILABLE, /* no microphone library or function */
    PV_MIC_FAILED       /* opening or reading failed: see mic_rc */
} pv_mic_state;

typedef enum {
    PV_OUT_OFF = 0,
    PV_OUT_OPENING,
    PV_OUT_ACTIVE,      /* the port is open and takes blocks (audibility unconfirmed) */
    PV_OUT_FAILED       /* opening or writing failed: see out_rc; incoming audio is still decoded */
} pv_out_state;

typedef struct {
    /* the user's choices */
    int in_voice;
    char party_id[64];
    int mic_enabled, muted, deafened;
    int out_volume, mic_gain, route;

    /* network */
    pv_net_state net;
    char net_error[200];
    int close_code;          /* of the last connection */
    int attempts;            /* since the last good connection */
    unsigned long sessions, connections, reconnects;
    int ready_seen, participants_seen;   /* in the current session */
    int ever_connected;                  /* since the service started */
    int latency_ms;
    uint64_t connected_since_ms;         /* 0 when not connected */
    uint64_t connected_total_ms;
    unsigned long sent, sent_nonzero, received, tx_dropped, rx_malformed;
    int daemon_ipc;                      /* the daemon's native interface (0 unknown / none) */
    char daemon_build[64];

    /* participants (from the gateway) */
    akn_voice_participant people[AKN_VOICE_MAX_PARTICIPANTS];
    int people_count;
    int gateway_self_level_max;          /* the gateway's level of this console's decoded audio */

    /* microphone */
    pv_mic_state mic;
    int32_t mic_rc;
    unsigned mic_rate;
    char mic_detail[160];
    unsigned long mic_opens, mic_reads, mic_read_errors, mic_frames, mic_nonzero_frames, encoded, encode_errors;
    int32_t mic_last_read_rc;
    int mic_level;                       /* 0..100 now */
    int mic_peak_percent;                /* since the microphone opened */
    double mic_rms_db, mic_rms_db_max;   /* the last second; the loudest second */
    int tx_tone_hz;

    /* output */
    pv_out_state out;
    int32_t out_rc;
    char out_detail[160];
    unsigned long out_opens, decoded, concealed, silent_frames, decoded_nonzero, queued_frames;
    unsigned long out_calls, out_errors, out_nonzero_blocks, underruns, late, overflow;
    int32_t out_last_rc;
    int paced;                           /* -1 unknown, 0 the calls return at once, 1 at playback speed */
    double out_call_ms_min, out_call_ms_avg, out_call_ms_max;
    int rx_level, rx_tone_hz;
    unsigned long test_sounds;
    int test_route;                      /* the last test sound: route, open result, calls, timing */
    int32_t test_open_rc;
    unsigned long test_calls;
    int test_paced;

    /* runtime */
    uint64_t uptime_ms;
    long long cpu_ms;
    int heartbeat_gap_max_ms;
    unsigned long heartbeats;
    int32_t bigapp;
    int game_running;
    unsigned long game_seconds_in_voice, game_changes;
    char last_failure[200];
    char backend[48];
    char platform[200];
} pv_status;

typedef struct {
    const pv_backend *be;
    char akeno_dir[256];   /* the daemon's data (native.key, settings.json, ca.pem): read only */
    char data_dir[256];    /* this service's report, log and settings */
    int daemon_port;
    int notifications;     /* system notifications for voice events */
} pv_config;

typedef struct pv_engine pv_engine;

pv_engine *pv_engine_new(const pv_config *config);
/* Leaves voice, waits (bounded) for the threads and writes the final report. */
void pv_engine_free(pv_engine *e);

/* The controls. Each returns 0, or -1 with a code and a sentence for the page. */
typedef struct {
    char code[40];
    char message[200];
} pv_error;

int pv_engine_join(pv_engine *e, const char *party_id, pv_error *err);
void pv_engine_leave(pv_engine *e, const char *why);
int pv_engine_set_mic(pv_engine *e, int enabled, pv_error *err);
int pv_engine_set_mute(pv_engine *e, int muted, pv_error *err);
void pv_engine_set_deafen(pv_engine *e, int deafened);
int pv_engine_set_volume(pv_engine *e, int percent);  /* returns the value set */
int pv_engine_set_gain(pv_engine *e, int percent);
int pv_engine_set_route(pv_engine *e, int route, pv_error *err);
int pv_engine_test_sound(pv_engine *e, pv_error *err);

void pv_engine_status(pv_engine *e, pv_status *out);
/* The main loop calls this every 100-250 ms: heartbeat, finished threads, game activity,
 * notifications and the report. */
void pv_engine_tick(pv_engine *e);
/* Writes the report now (the exit path). */
void pv_engine_write_report(pv_engine *e);

const char *pv_net_state_name(pv_net_state s);
const char *pv_mic_state_name(pv_mic_state s);
const char *pv_out_state_name(pv_out_state s);

/* A party ID as the server issues it: 1-64 of [A-Za-z0-9-]. */
int pv_party_id_ok(const char *id);

#endif
