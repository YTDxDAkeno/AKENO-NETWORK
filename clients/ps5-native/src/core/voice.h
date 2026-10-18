/*
 * Party voice session model (portable, unit-tested): the states the Party screen shows, the
 * reconnect policy, the user's controls (mute, deafen, volumes) and the room's participants as the
 * gateway reports them. The threads that move audio live in src/app/voice.c.
 */
#ifndef AKN_VOICE_H
#define AKN_VOICE_H

#include <stdint.h>

#include "voice_proto.h"

typedef enum {
    AKN_VOICE_OFF = 0,
    AKN_VOICE_REQUESTING,     /* asking the AKENO server (through the daemon) for a voice pass */
    AKN_VOICE_CONNECTING,     /* TLS + WebSocket + hello */
    AKN_VOICE_CONNECTED,
    AKN_VOICE_RECONNECTING,   /* lost; a new pass and connection follow after a pause */
    AKN_VOICE_FAILED          /* stopped with an error (see the error line) */
} akn_voice_state;

typedef enum {
    AKN_MIC_CLOSED = 0,
    AKN_MIC_OPEN,
    AKN_MIC_UNAVAILABLE,      /* the system library or function is missing */
    AKN_MIC_FAILED            /* opening or reading failed: see the code */
} akn_mic_state;

#define AKN_VOICE_VOLUME_STEP 10
#define AKN_VOICE_VOLUME_MAX 200
#define AKN_VOICE_RECONNECT_MAX 6

typedef struct {
    akn_voice_state state;
    char party_id[64];
    char party_name[96];
    int muted;
    int deafened;
    int out_volume;           /* percent, 0..200 */
    int mic_volume;           /* percent, 0..200 */
    akn_mic_state mic;
    int32_t mic_rc;           /* last microphone call result (0 or a system error code) */
    int mic_level;            /* 0..100, local meter */
    int mic_rate;             /* the rate the microphone port opened with */
    int self_speaking;
    akn_voice_participant people[AKN_VOICE_MAX_PARTICIPANTS];
    int people_count;
    int attempts;             /* reconnect attempts since the last good connection */
    uint64_t retry_at;        /* when the next attempt starts (ms) */
    char error[200];
    uint64_t connected_at;
    /* statistics for the report (counts and levels only) */
    unsigned long sent, received, concealed, underruns, late, overflow, reconnects;
    int rx_level;             /* 0..100, what is played */
    int rx_tone_hz, tx_tone_hz;  /* single-tone estimate (diagnostics, PC test) */
    int latency_ms;           /* last ping round trip */
    /* the local microphone test (nothing is sent) */
    int mic_test;             /* running */
    int mic_monitor;          /* the test plays the microphone back (headset recommended) */
    int mic_peak;             /* 0..32767 since the test started */
    unsigned long mic_frames; /* 20 ms frames captured (voice or test) */
    char mic_detail[160];     /* why the microphone is unavailable, or the backend */
} akn_voice;

void akn_voice_init(akn_voice *v);
const char *akn_voice_state_text(const akn_voice *v);
const char *akn_mic_state_text(akn_mic_state s);

/* Volume steps; return the new value. */
int akn_voice_volume_step(int value, int direction);

/* Whether a closed connection should be retried (removal, replacement and room end are final). */
int akn_voice_should_retry(int close_code, int attempts);
/* Pause before attempt n (1-based): 1 s, 2 s, 4 s, 8 s, then 10 s. */
uint32_t akn_voice_backoff_ms(int attempt);

/* Applies a participant list from the gateway; marks the user's own entry. */
void akn_voice_set_people(akn_voice *v, const akn_voice_msg *msg);
/* Whether a party member is speaking right now (by user ID). */
int akn_voice_is_speaking(const akn_voice *v, const char *user_id);
int akn_voice_in_room(const akn_voice *v, const char *user_id);

#endif
