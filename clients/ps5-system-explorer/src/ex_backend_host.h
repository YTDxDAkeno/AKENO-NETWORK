/*
 * The PC simulation of a PS5 for the Explorer's tests. A virtual clock makes the timeline
 * deterministic and instant; every console answer can be bent to a failure the modules must
 * handle. Not a PS5: a passing simulated run is never evidence of console behaviour.
 */
#ifndef AKN_EX_BACKEND_HOST_H
#define AKN_EX_BACKEND_HOST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    /* clock */
    int real_time;                /* 0: virtual clock (tests); 1: real monotonic clock */
    uint64_t vclock_us;

    /* process answers */
    int have_fw;
    uint32_t fw;                  /* e.g. 0x12200000 */
    int have_proc;                /* the per-process query is decoded (layout-compatible) */
    int foreground;               /* this process is the running foreground app */
    int have_appinfo;
    int32_t appinfo_err;          /* appinfo_rc when not foreground (background payload) */
    int appinfo_ok_zero;          /* background: appinfo succeeds (rc 0) with app id 0 (owner's 6.1 case) */
    int have_bigapp;
    int have_user;
    int32_t user_rc;
    int have_rlimit, have_rusage;

    /* 6.1: how the per-process query behaves. Default (zeroed) models the FW 12.20 observation:
     * NOT_AVAILABLE with no decoded record. Set pq_* to drive a specific failure or success. */
    int pq_mode;                  /* 0 empty-read; 1 decoded; 2 size-denied; 3 unsupported; 4 ENOMEM; 5 layout-mismatch */
    long long pq_maxrss_end;      /* peak rss reported by read_usage at the end (0 = same as start) */

    /* 6.1: title-ID answers, keyed by running app id. */
    int title_no_fn;              /* sceSystemServiceGetAppTitleId not exported */
    const char *title_map;        /* "0x60000123=PPSA00001;0x4018=" lines; empty value = error rc */
    int32_t title_err_rc;         /* rc for an app id mapped to an empty title or not found */
    int title_overrun;            /* the call writes one byte past its window (guard must catch it) */
    int title_malformed;          /* the call returns a non-title-shaped string */
    unsigned title_calls;         /* counter (observability) */

    /* context scripting */
    int32_t game_app;             /* the game's app id, e.g. 0x60000123 */
    int game_after_samples;       /* <0 never; else the running app becomes the game after N samples */
    uint32_t user_id;

    /* library behaviour (by catalogue index) */
    unsigned absent_files;        /* bit i: library i's file is absent */
    unsigned load_fail;           /* bit i: dlopen of library i fails */
    unsigned missing_last_sym;    /* bit i: library i's last symbol is absent */

    /* control script (returned to read_control in chunks) */
    const char *control;
    size_t control_pos;
    unsigned control_chunk;       /* bytes to hand out per call (0 = all remaining) */
    const char *control2;         /* 6.1: a replacement control stream; swapped in at control_swap_at */
    unsigned control_swap_at;     /* sample call number to swap control -> control2 (0 = never) */
    int control_reset_pending;

    /* 6.1: checkpoint takeover - return nonzero from the checkpoint at this heartbeat number. */
    unsigned supersede_at_checkpoint;
    unsigned checkpoint_calls;

    /* gap injection */
    unsigned gap_at_poll;         /* on sleep call number this, add gap_ms to the virtual clock */
    int gap_ms;

    /* counters / capture */
    unsigned sample_calls;
    unsigned sleep_calls;
    unsigned long notifications;
    char last_notification[256];
} ex_sim;

#endif
