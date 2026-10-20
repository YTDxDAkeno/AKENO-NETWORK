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
    int have_proc;
    int foreground;               /* this process is the running foreground app */
    int have_appinfo;
    int32_t appinfo_err;          /* appinfo_rc when not foreground (background payload) */
    int have_bigapp;
    int have_user;
    int32_t user_rc;
    int have_rlimit, have_rusage;

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
