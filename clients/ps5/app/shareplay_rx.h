/*
 * SharePlay Player 2 diagnostic receiver (Alpha 0.5, experimental).
 *
 * The AKENO server relays the SharePlay guest's controller state to the host's paired console
 * app as realtime events ("shareplay.input"). This module validates and orders those states
 * with the same rules as the browser monitor (packages/shared/src/shareplay/controller-protocol.ts):
 * strict field checks, a new grant restarts numbering, only newer sequence numbers apply,
 * releases always apply, and everything returns to neutral after AKENO_SP_WATCHDOG_MS without
 * fresh input.
 *
 * It ONLY records and displays the state. It does not create a virtual controller and does not
 * inject input anywhere: no verified, safe interface for that exists for PS5 payloads
 * (docs/SHAREPLAY_STATUS.md). The adapter functions below say so and always fail.
 *
 * Portable C11 (unit-tested on Linux); not thread-safe by itself (the app guards it with
 * app->lock).
 */
#ifndef AKENO_SHAREPLAY_RX_H
#define AKENO_SHAREPLAY_RX_H

#include <stdint.h>

#include "akeno/json.h"
#include "buf.h"

#define AKENO_SP_PROTOCOL_VERSION 1
#define AKENO_SP_WATCHDOG_MS 500
#define AKENO_SP_SLOT_PLAYER_2 2
#define AKENO_SP_AXIS_MAX 32767
#define AKENO_SP_BUTTON_COUNT 18
/* PS/Home (16) and Create (8) are never accepted from a remote guest. */
#define AKENO_SP_BLOCKED_BUTTONS ((1u << 16) | (1u << 8))
#define AKENO_SP_ID_MAX 40

typedef struct {
    uint32_t buttons;
    int32_t axes[4];     /* left x, left y, right x, right y: -32767..32767 */
    int32_t triggers[2]; /* 0..255 */
} akeno_sp_state;

typedef enum {
    AKENO_SP_APPLIED = 1,
    AKENO_SP_STALE = 0,   /* well-formed but older than what was applied: dropped */
    AKENO_SP_INVALID = -1 /* malformed: dropped */
} akeno_sp_result;

typedef struct {
    char session_id[AKENO_SP_ID_MAX];
    char grant_id[AKENO_SP_ID_MAX];
    char guest_id[AKENO_SP_ID_MAX];
    uint32_t epoch;
    uint32_t last_seq;
    int have_grant;
    akeno_sp_state state; /* last applied state (before the watchdog) */
    long long last_at;    /* local time (ms) of the last applied packet; 0 = never */
    long long first_at;
    unsigned long long accepted;
    unsigned long long stale;
    unsigned long long invalid;
    unsigned long long releases;
    unsigned long long watchdog_trips;
    int tripped; /* the watchdog is holding neutral right now */
    char last_reason[160];
    /* Packets applied during the last full second, for the page. */
    long long window_start;
    unsigned window_count;
    unsigned rate;
} akeno_sp_rx;

void akeno_sp_rx_init(akeno_sp_rx *rx);

/* Applies the data object of a "shareplay.input" event. */
akeno_sp_result akeno_sp_rx_apply(akeno_sp_rx *rx, const akeno_json *data, long long now_ms);

/*
 * The state to use right now: the last applied one, or neutral when nothing valid arrived for
 * AKENO_SP_WATCHDOG_MS. Returns 1 when the watchdog forced neutral.
 */
int akeno_sp_rx_current(akeno_sp_rx *rx, long long now_ms, akeno_sp_state *out);

/* {"receiver":{...},"capabilities":{...}} for the console page. Returns 0 on success. */
int akeno_sp_rx_write_json(akeno_sp_rx *rx, long long now_ms, akeno_buf *out);

/*
 * Platform adapters. Both are UNSUPPORTED in this version and never touch the system:
 * - virtual controller: the SDK lists scePadVirtualDevice* symbols but declares no signatures;
 *   known community uses depend on guessed structures, kernel credential changes and code
 *   injected into SceShellCore, which AKENO does not do.
 * - native capture: the SDK has no screenshot, recording or capture-encoder interface.
 * They return -1 and describe why.
 */
int akeno_sp_virtual_pad_open(const char **reason);
int akeno_sp_native_capture_open(const char **reason);

#endif
