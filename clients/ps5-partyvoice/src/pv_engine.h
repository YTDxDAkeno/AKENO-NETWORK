/*
 * The voice engine of AKENO Party Voice: one party voice session at a time, its threads, and what
 * they measured. The control interface (pv_control.c) and the report (pv_status.c) read it through
 * pv_engine_status(); nothing else touches the threads.
 *
 *   network   voice pass from the AKENO server through the daemon (IPC 3), TLS WebSocket to the
 *             gateway on the server's own host, hello, mute/deafen, received Opus into the jitter
 *             buffer, queued Opus out; reconnects with a new pass after a loss
 *   playback  jitter buffer -> Opus decode / concealment -> (+ test sound) -> volume -> sceAudioOut
 *             (the chosen route). The port is probed once (silent blocks back to back: does an audio
 *             clock consume them?) and then paces the thread by blocking; the thread only sleeps
 *             when the port runs more than a few blocks ahead of real time (it discards)
 *   capture   only after the user turned the microphone on: sceAudioIn -> 48 kHz -> gain -> Opus.
 *             A refusal is classified, and the same refused call is not repeated in the same
 *             situation (same user, same running app)
 *   test      N beeps of 880 Hz on route N, Opus-encoded and decoded by the same code as party
 *             speech, mixed in before the volume; every submitted block is checked for the tone
 *
 * Every audio boundary is counted: frames read and nonzero, packets encoded and sent, packets
 * received and decoded, output calls and their timing. A successful call is never reported as
 * audible sound or as a microphone that works: the owner's ears and a friend decide that.
 */
#ifndef PV_ENGINE_H
#define PV_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "context.h"
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

/* Why the console refused the microphone (or an output port), from its code. The meanings of the
 * 0x8026xxxx codes are the PS4 SDK names that emulators (shadPS4) carry; 0x809B0081 has no public
 * meaning: it is what background payloads get from scePadOpen on 12.20 and 13.60, and what this
 * background service got from sceAudioInOpen on 12.20, while AKENO Native, the foreground app,
 * opened the microphone with the same call. */
typedef enum {
    PV_REFUSAL_NONE = 0,
    PV_REFUSAL_PROCESS,   /* 0x809B0081: refused to this process (background payload) */
    PV_REFUSAL_RATE,      /* 0x80260103 (audio in) / 0x80260008 (audio out): sample rate not accepted */
    PV_REFUSAL_ARGUMENT,  /* another argument code of the library (0x802601xx / 0x8026000x) */
    PV_REFUSAL_RESOURCE,  /* 0x80260011 SYSTEM_RESOURCE: no port for this user (route 2 on 12.20) */
    PV_REFUSAL_BUSY,      /* port full / busy: someone else holds it; may change, so it may be tried again */
    PV_REFUSAL_NO_USER,   /* no foreground user (user service) */
    PV_REFUSAL_NO_LIBRARY,/* libSceAudioIn.sprx or a function missing */
    PV_REFUSAL_OTHER
} pv_refusal_kind;

pv_refusal_kind pv_refusal_of(int32_t rc, int microphone);
const char *pv_refusal_name(pv_refusal_kind k);     /* "process", "rate", ... for the status document */
const char *pv_refusal_meaning(pv_refusal_kind k);  /* one sentence for the report */
/* 1 when the refusal may go away by itself (the call may be repeated later in the same situation). */
int pv_refusal_transient(pv_refusal_kind k);

/* Does an audio clock consume the port? From the time spent inside output calls 2..n against the
 * audio those calls carried: 1 yes (the calls block about one block each), 0 no (they return at
 * once: the samples are not played at playback speed), -1 too few calls or in between. */
int pv_clock_verdict(double blocked_ms, double audio_ms);

typedef enum {
    PV_TEST_NONE = 0,
    PV_TEST_PLAYING,
    PV_TEST_SUBMITTED,      /* the tone reached the output call in nonzero blocks that were accepted */
    PV_TEST_NOT_PLAYED,     /* the route's port could not be opened: nothing was played */
    PV_TEST_OUTPUT_FAILED   /* the port took no tone block */
} pv_test_state;

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
    unsigned long mic_open_calls;        /* sceAudioInOpen calls in this run (each configuration counts) */
    unsigned long mic_open_ok;           /* of them successful */
    unsigned long mic_avoided;           /* "turn on" requests answered from a known refusal, without a call */
    pv_refusal_kind mic_refusal;         /* of the last failed open */
    int32_t mic_refused_rc;              /* the last refusal in this run (kept across sessions, for the report) */
    pv_refusal_kind mic_refused_kind;
    char mic_attempts[160];              /* the last turn-on: "16000 Hz -> 0x809B0081" */
    int32_t mic_last_read_rc;
    int mic_level;                       /* 0..100 now */
    int mic_peak_percent;                /* since the microphone opened */
    double mic_rms_db, mic_rms_db_max;   /* the last second; the loudest second */
    int tx_tone_hz;

    /* output */
    pv_out_state out;
    int32_t out_rc;
    char out_detail[160];
    pv_refusal_kind out_refusal;
    unsigned long out_opens, decoded, concealed, silent_frames, decoded_nonzero, queued_frames;
    unsigned long out_calls, out_errors, out_nonzero_blocks, underruns, late, overflow;
    unsigned long out_speech_blocks;     /* accepted blocks in which party speech had sound (after the volume) */
    unsigned long out_avoided;           /* port opens answered from a known refusal, without a call */
    int out_speech_peak;                 /* loudest speech sample submitted, percent of full scale */
    int32_t out_last_rc;
    int paced;                           /* pv_clock_verdict of the current port (-1 unknown) */
    double out_call_ms_min, out_call_ms_avg, out_call_ms_max;
    double out_blocked_ms, out_audio_ms; /* calls 2..n: time inside the call, audio carried */
    unsigned long out_self_sleeps;       /* the port ran ahead of real time and the thread slept */
    /* the port clock probe: PV_PROBE_BLOCKS silent blocks back to back right after the open */
    int probe_verdict;                   /* pv_clock_verdict, or -2 not run */
    int probe_blocks;
    double probe_ms;                     /* inside calls 2..n of the probe */
    int32_t probe_rc;
    int rx_level, rx_tone_hz;
    unsigned long test_sounds;
    /* the last test sound */
    pv_test_state test_state;
    int test_route;
    int test_in_voice;                   /* 1 mixed into the voice output, 0 on its own */
    int32_t test_open_rc;
    char test_detail[160];               /* the port it used (user, port type, format), or why none */
    unsigned long test_calls;            /* accepted output calls while the tone played */
    unsigned long test_tone_blocks;      /* accepted blocks that carried the tone (nonzero) */
    unsigned long test_verified_blocks;  /* of them: the tone measured in the submitted buffer at 880 Hz +-5 % */
    int test_hz_min, test_hz_max;        /* measured in the submitted buffer (left channel) */
    int test_peak_percent;               /* loudest submitted tone sample */
    unsigned long test_encoded, test_decoded; /* Opus packets of the tone */
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
    /* this process (AKENO Native's context check): a background payload or the foreground app */
    akn_context_kind process_kind;
    char process_detail[200];
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
